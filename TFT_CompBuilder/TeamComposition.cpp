#include "TeamComposition.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>
using namespace std;

namespace {
	struct ChampionVariantData {
		string canonicalName;
		string label;
		int width;
		map<string, int> traits;
	};

	string getVariantLabel(const string& championName, const pair<string, int>& choice) {
		string label = championName + "[" + choice.first;
		if (choice.second != 1) label += ":" + to_string(choice.second);
		return label + "]";
	}

	bool sharesConnectingTrait(const map<string, int>& left, const map<string, int>& right) {
		for (const auto& trait : left) {
			if (trait.first != "Threat" && right.contains(trait.first)) return true;
		}
		return false;
	}
}

bool TeamComposition::initialized = false;
bool TeamComposition::gateTableInitialized = false;
ChampSet TeamComposition::dragons = 0;
ChampSet TeamComposition::scalescorns = 0;
unordered_map<string, Champion> TeamComposition::globalChampInfoMap;
int TeamComposition::championVariantCount = 0;
unordered_map<string, vector<int>> TeamComposition::currentSetTraits;
unordered_map<string, vector<string>> TeamComposition::championGraph;
unordered_map<string, ChampSet> TeamComposition::championBitsetGraph;
unordered_map<string, int> TeamComposition::champStringToBitPosMap;
unordered_map<string, ChampSet> TeamComposition::champStringToBitsetMap;
string TeamComposition::champBitPosToStringMap[128];
string TeamComposition::traitArrPosToStringMap[64];
unordered_map<string, short> TeamComposition::traitStringToArrPosMap;
short TeamComposition::champWidthByBitPos[128];
ChampSet TeamComposition::championConnectionsByBitPos[128];
ChampSet TeamComposition::championIdentityByBitPos[128];
vector<TeamComposition::TraitDelta> TeamComposition::champTraitDeltasByBitPos[128];
GateTable TeamComposition::currentGateTable;

void TeamComposition::setGateTable(const GateTable& gateTable) {
	currentGateTable = gateTable;
	gateTableInitialized = true;
}

//Returns the amount of active trait tiers
int TeamComposition::getActiveTraitTiersTotal() const {
	if (currentSetTraits.size() == 0) throw runtime_error("Current Set Traits not initialized.");
	int total = 0;
	for (int i = 0; i < currentSetTraits.size(); ++i) {
		string trait = traitArrPosToStringMap[i];
		int traitVal = compTraits[i];
		const vector<int>& traitMilestones = currentSetTraits.at(trait);
		//loop through milestones, add n to total where n is the last nth milestone that the trait is greater than
		for (int j = 0; j < traitMilestones.size(); ++j) {
			if (traitVal < traitMilestones.at(j)) { 
				total += j;
				break;
			}
			else if (j == traitMilestones.size()-1) {
				total += j+1;
			}
		}
	}
	return total;
}

//Returns the amount of active unique traits
int TeamComposition::getActiveTraitsTotal() const {
	if (currentSetTraits.size() == 0) throw runtime_error("Current Set Traits not initialized.");
	int total = 0;
	for (int i = 0; i < currentSetTraits.size(); ++i) {
		string trait = traitArrPosToStringMap[i];
		int traitVal = compTraits[i];
		//count trait as active if the comp has an amount of trait greater than or equal to the first milestone of the trait
		const vector<int>& traitMilestones = currentSetTraits.at(trait);
		if (traitVal >= traitMilestones.at(0)) ++total; 																					
	}
	return total;
}

bool TeamComposition::containsChamp(const string& champion) const {
	auto it = champStringToBitsetMap.find(champion);
	if (it == champStringToBitsetMap.end()) return false;
	return (champions & it->second).any();
}

//Returns a string representation of the comp
string TeamComposition::toString() const {
	string s = "";
	int champCount = 0;
	for (int i = 0; i < championVariantCount; ++i) {
		if (champions.test(i)) {
			s += " " + champBitPosToStringMap[i];
			++champCount;
			if (champCount == MAX_COMP_SIZE) return s;
		}
	}
	return s;
}

//Adds a champ to the comp and updates compTraits and connectedChamps accordingly. Returns true if champ was added.
bool TeamComposition::addChamp(const string& champ) {
	auto exactVariant = champStringToBitPosMap.find(champ);
	if (exactVariant != champStringToBitPosMap.end()) return addChamp(exactVariant->second);

	auto canonicalChampion = champStringToBitsetMap.find(champ);
	if (canonicalChampion == champStringToBitsetMap.end()) {
		throw runtime_error("Unknown champion or champion variant \"" + champ + "\".");
	}
	throw runtime_error(
		"Champion \"" + champ + "\" has interchangeable traits. Add an explicit variant such as " + champ + "[Trait]."
	);
}

bool TeamComposition::addChamp(int champBitPos) {
	if (blockedChampionVariants.test(champBitPos)) return false;
	champions.set(champBitPos); //Add the champ to the comp, duplicates are not added
	blockedChampionVariants |= championIdentityByBitPos[champBitPos];

	for (const TraitDelta& trait : champTraitDeltasByBitPos[champBitPos]) {
		compTraits[trait.traitPos] += trait.traitValue;
	}

	connectedChamps |= championConnectionsByBitPos[champBitPos]; //adds champ's connected champs to the comp's connected champs
	connectedChamps &= (~blockedChampionVariants); //removes the selected champion and all mutually exclusive variants

	compSize += champWidthByBitPos[champBitPos]; //increases the comp's size by the champ's width
	return true;
}

void TeamComposition::incrementTrait(const string& trait) {
	auto it = traitStringToArrPosMap.find(trait);
	if (it == traitStringToArrPosMap.end()) {
		throw runtime_error("Cannot add emblem for unknown trait \"" + trait + "\".");
	}
	++compTraits[it->second];
}

TeamComposition::CompSet TeamComposition::buildNextCompSet(const CompSet& compSet, int targetCompSize, int iterationCompSize, const int settings[3], int gateBound, int prevTraitValMax, int& currTraitValMax, double& elapsedSeconds, double timeoutSeconds, bool* timedOut) {
	const int champCount = championVariantCount;
	CompSet nextCompSet;
	currTraitValMax = 0;
	auto iterationStart = std::chrono::steady_clock::now();
	if (timedOut) *timedOut = false;

	auto addCandidate = [&](const TeamComposition& candidate) {
		// A wide champion can advance beyond this board-width layer. Carry that
		// composition forward and apply pruning when its occupied width is reached.
		if (candidate.compSize > iterationCompSize) {
			nextCompSet.emplace(candidate);
			return;
		}

		if (!settings[0]) {
			int traitValue = (settings[1] == 1 ? candidate.getActiveTraitTiersTotal() : candidate.getActiveTraitsTotal());
			if (traitValue > currTraitValMax) currTraitValMax = traitValue;
			if (settings[1] == 2) {
				if (traitValue < prevTraitValMax + 1) return;
			}
			else if (traitValue < gateBound) {
				return;
			}
		}

		nextCompSet.emplace(candidate);
	};

	for (const TeamComposition& currComp : compSet) {
		if (currComp.compSize >= iterationCompSize) {
			if (currComp.compSize <= targetCompSize) addCandidate(currComp);
			continue;
		}

		ChampSet connections;
		if (settings[2] && currComp.size() > 0) connections = currComp.connectedChamps; //only consider champs that share traits with the current comp's champs
		else connections = ~currComp.blockedChampionVariants; //consider every champion whose canonical identity is not already in the comp
		connections &= ~currComp.blockedChampionVariants;

		for (int i = 0; i < champCount; ++i) {
			if (timeoutSeconds >= 0.0) {
				elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - iterationStart).count();
				if (elapsedSeconds > timeoutSeconds) {
					if (timedOut) *timedOut = true;
					return nextCompSet;
				}
			}

			bool champConnected = connections.test(i); //if champ is supposed to be considered
			bool champFits = champWidthByBitPos[i] <= (targetCompSize - currComp.compSize); // for set 7 dragons
			if (!champConnected || !champFits) continue;

			TeamComposition nextComp(currComp);
			nextComp.addChamp(i);
			addCandidate(nextComp);
		}
	}

	elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - iterationStart).count();
	return nextCompSet;
}

//Generates and returns a list of comps given a target comp size and list of settings
//settings[] guide: 
//  settings[0] - use pruning or not
//  settings[1] - Pruning mode. 0 = trait gates; 1 = tier gates; 2 = dynamic
//  settings[2] - use all champs or only connectedChamps;
//Example settings:
//  Default - {0,0,0} 
//  All possible comps - {1,0,0}
//  Use tier gates, not trait gates - {0,1,0}
//  Use dynamic pruning - {0,2,0}
vector<TeamComposition> TeamComposition::generateComps(int compSize, int settings[3], const TeamComposition& seedComp) {
	if (compSize > 10 || compSize < 1) throw runtime_error("generateComps must have parameter compSize between 1 and 10 (inclusive).");
	if (!settings[0] && settings[1] != 2 && !gateTableInitialized) throw runtime_error("Gate table not initialized.");

	int currCompSize = 0;
	CompSet compSet; //holds the previous while loop iteration's generated comps
	compSet.emplace(seedComp);

	int prevTraitValMax = 0;
	int currTraitValMax = 0;
	double elapsedSeconds = 0.0;

	//BFS over comps
	while (currCompSize < compSize) { 
		++currCompSize;
		int gateBound = 0;
		if (!settings[0] && settings[1] != 2) {
			gateBound = (settings[1] == 1
				? currentGateTable.activeTierGates[compSize - 1][currCompSize - 1]
				: currentGateTable.activeTraitGates[compSize - 1][currCompSize - 1]);
		}
		CompSet nextCompSet = buildNextCompSet(compSet, compSize, currCompSize, settings, gateBound, prevTraitValMax, currTraitValMax, elapsedSeconds);
		prevTraitValMax = currTraitValMax;
		compSet.swap(nextCompSet);
	}

	//copy final set of comps to a list
	vector<TeamComposition> compList;
	for (const TeamComposition &comp : compSet) {
		compList.push_back(comp);
	}

	return compList;
}

vector<TeamComposition> TeamComposition::generateComps(int compSize, int settings[3]) {
	return generateComps(compSize, settings, TeamComposition());
}

//Calls generateComps with default settings
vector<TeamComposition> TeamComposition::generateComps(int compSize) {
	int traitSettings[3] = { 0,0,0 };
	return generateComps(compSize, traitSettings);
}

GateTable TeamComposition::calculateGateTable(bool recalculateFromScratch, int timeoutSeconds, int maxTargetCompSize, int pruningMode, const TeamComposition& seedComp, bool connectedChampsOnly) {
	if (!initialized) throw runtime_error("TeamComposition statics not initialized.");
	if (timeoutSeconds < 1) throw runtime_error("Gate calculation timeout must be at least 1 second.");
	if (!recalculateFromScratch && !gateTableInitialized) throw runtime_error("Gate table not initialized.");
	if (maxTargetCompSize < 1 || maxTargetCompSize > MAX_COMP_SIZE) {
		throw runtime_error("Gate calculation target size must be between 1 and " + to_string(MAX_COMP_SIZE) + ".");
	}
	if (pruningMode != 0 && pruningMode != 1) {
		throw runtime_error("Gate calculation pruning mode must be 0 (trait gates) or 1 (tier gates).");
	}

	GateTable calculatedGates = gateTableInitialized ? currentGateTable : GateTable{};

	int targetCompSize = maxTargetCompSize;
	{
		CompSet compSet;
		compSet.emplace(seedComp);
		int prevTraitValMax = 0;
		int previousAcceptedGate = 0;

		for (int iterationCompSize = 1; iterationCompSize <= targetCompSize; ++iterationCompSize) {
			int settings[3] = { 0, pruningMode, connectedChampsOnly ? 1 : 0 };
			int minimumGateBound = (iterationCompSize > 1) ? previousAcceptedGate : 0;
			int gateBound = 0;
			if (recalculateFromScratch) {
				if (targetCompSize > 1 && iterationCompSize < targetCompSize) {
					gateBound = (pruningMode == 1)
						? calculatedGates.activeTierGates[targetCompSize - 2][iterationCompSize - 1]
						: calculatedGates.activeTraitGates[targetCompSize - 2][iterationCompSize - 1];
				}
				else if (iterationCompSize > 1) {
					gateBound = previousAcceptedGate;
				}
			}
			else {
				gateBound = max(0, pruningMode == 1
					? currentGateTable.activeTierGates[targetCompSize - 1][iterationCompSize - 1]
					: currentGateTable.activeTraitGates[targetCompSize - 1][iterationCompSize - 1]);
			}
			gateBound = max(gateBound, minimumGateBound);
			int highestSlowBound = -1;
			int lowestEmptyBound = std::numeric_limits<int>::max();
			int weakestSuccessGate = -1;
			int weakestSuccessTraitValue = 0;
			CompSet weakestSuccessCompSet;

			while (true) {
				int currentLayerMaxTraitValue = 0;
				double currentLayerElapsedSeconds = 0.0;
				CompSet nextCompSet = buildNextCompSet(compSet, targetCompSize, iterationCompSize, settings, gateBound, prevTraitValMax, currentLayerMaxTraitValue, currentLayerElapsedSeconds);
				cout << "Gate calc | mode=" << pruningMode
					<< " target=" << targetCompSize
					<< " iter=" << iterationCompSize
					<< " gate=" << gateBound
					<< " frontier_elapsed=" << currentLayerElapsedSeconds << "s"
					<< " frontier_comps=" << nextCompSet.size();

				if (nextCompSet.empty()) {
					cout << " result=empty_frontier" << endl;
					lowestEmptyBound = min(lowestEmptyBound, gateBound);
					if (weakestSuccessGate >= 0) {
						if (gateBound < weakestSuccessGate) {
							int acceptedGate = (iterationCompSize == targetCompSize) ? weakestSuccessTraitValue : weakestSuccessGate;
							if (pruningMode == 1) calculatedGates.activeTierGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
							else calculatedGates.activeTraitGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
							compSet.swap(weakestSuccessCompSet);
							prevTraitValMax = weakestSuccessTraitValue;
							previousAcceptedGate = weakestSuccessGate;
							break;
						}
						if (highestSlowBound + 1 >= weakestSuccessGate) {
							int acceptedGate = (iterationCompSize == targetCompSize) ? weakestSuccessTraitValue : weakestSuccessGate;
							if (pruningMode == 1) calculatedGates.activeTierGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
							else calculatedGates.activeTraitGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
							compSet.swap(weakestSuccessCompSet);
							prevTraitValMax = weakestSuccessTraitValue;
							previousAcceptedGate = weakestSuccessGate;
							break;
						}
						int lowerSearchBound = max(highestSlowBound, minimumGateBound - 1);
						gateBound = lowerSearchBound + max(1, (weakestSuccessGate - lowerSearchBound) / 2);
						continue;
					}

					if (gateBound == minimumGateBound) {
						throw runtime_error(
							"Unable to find a non-empty gate for pruning mode " + to_string(pruningMode) +
							", target size " + to_string(targetCompSize) +
							", iteration size " + to_string(iterationCompSize) + "."
						);
					}
					gateBound = max(minimumGateBound, gateBound - 1);
					continue;
				}

				if (iterationCompSize < targetCompSize) {
					int probeMaxTraitValue = 0;
					double probeElapsedSeconds = 0.0;
					bool timedOut = false;
					CompSet probeCompSet = buildNextCompSet(nextCompSet, targetCompSize, iterationCompSize + 1, settings, 0, currentLayerMaxTraitValue, probeMaxTraitValue, probeElapsedSeconds, (double)timeoutSeconds, &timedOut);
					cout << " probe_elapsed=" << probeElapsedSeconds << "s"
						<< " probe_comps=" << probeCompSet.size();

					if (timedOut) {
						cout << " result=timeout" << endl;
						highestSlowBound = max(highestSlowBound, gateBound);
						if (weakestSuccessGate >= 0) {
							if (highestSlowBound + 1 >= weakestSuccessGate) {
								int acceptedGate = weakestSuccessGate;
								if (pruningMode == 1) calculatedGates.activeTierGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
								else calculatedGates.activeTraitGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
								compSet.swap(weakestSuccessCompSet);
								prevTraitValMax = weakestSuccessTraitValue;
								previousAcceptedGate = weakestSuccessGate;
								break;
							}
							int lowerSearchBound = max(highestSlowBound, minimumGateBound - 1);
							gateBound = lowerSearchBound + max(1, (weakestSuccessGate - lowerSearchBound) / 2);
							continue;
						}
						if (lowestEmptyBound != std::numeric_limits<int>::max() && highestSlowBound + 1 >= lowestEmptyBound) {
							throw runtime_error(
								"Unable to satisfy timeout while keeping non-empty results for pruning mode " + to_string(pruningMode) +
								", target size " + to_string(targetCompSize) +
								", iteration size " + to_string(iterationCompSize) + "."
							);
						}
						gateBound = (lowestEmptyBound == std::numeric_limits<int>::max())
							? gateBound + 1
							: highestSlowBound + max(1, (lowestEmptyBound - highestSlowBound) / 2);
						gateBound = max(gateBound, minimumGateBound);
						continue;
					}

					if (probeCompSet.empty()) {
						cout << " result=empty_probe" << endl;
						lowestEmptyBound = min(lowestEmptyBound, gateBound);
						if (weakestSuccessGate >= 0) {
							if (gateBound < weakestSuccessGate) {
								int acceptedGate = weakestSuccessGate;
								if (pruningMode == 1) calculatedGates.activeTierGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
								else calculatedGates.activeTraitGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
								compSet.swap(weakestSuccessCompSet);
								prevTraitValMax = weakestSuccessTraitValue;
								previousAcceptedGate = weakestSuccessGate;
								break;
							}
							if (highestSlowBound + 1 >= weakestSuccessGate) {
								int acceptedGate = weakestSuccessGate;
								if (pruningMode == 1) calculatedGates.activeTierGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
								else calculatedGates.activeTraitGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
								compSet.swap(weakestSuccessCompSet);
								prevTraitValMax = weakestSuccessTraitValue;
								previousAcceptedGate = weakestSuccessGate;
								break;
							}
							int lowerSearchBound = max(highestSlowBound, minimumGateBound - 1);
							gateBound = lowerSearchBound + max(1, (weakestSuccessGate - lowerSearchBound) / 2);
							continue;
						}

						if (gateBound == minimumGateBound) {
							throw runtime_error(
								"Unable to find a gate that preserves a non-empty next iteration for pruning mode " + to_string(pruningMode) +
								", target size " + to_string(targetCompSize) +
								", iteration size " + to_string(iterationCompSize) + "."
							);
						}
						gateBound = max(minimumGateBound, gateBound - 1);
						continue;
					}

					cout << " result=success" << endl;
				}
				else {
					cout << " result=success" << endl;
				}

				if (weakestSuccessGate < 0 || gateBound < weakestSuccessGate) {
					weakestSuccessGate = gateBound;
					weakestSuccessTraitValue = currentLayerMaxTraitValue;
					weakestSuccessCompSet.swap(nextCompSet);
				}

				if (weakestSuccessGate == minimumGateBound || highestSlowBound + 1 >= weakestSuccessGate) {
					int acceptedGate = (iterationCompSize == targetCompSize) ? weakestSuccessTraitValue : weakestSuccessGate;
					if (pruningMode == 1) calculatedGates.activeTierGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
					else calculatedGates.activeTraitGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
					compSet.swap(weakestSuccessCompSet);
					prevTraitValMax = weakestSuccessTraitValue;
					previousAcceptedGate = weakestSuccessGate;
					break;
				}

				int lowerSearchBound = max(highestSlowBound, minimumGateBound - 1);
				gateBound = lowerSearchBound + max(1, (weakestSuccessGate - lowerSearchBound) / 2);
			}
		}
	}

	setGateTable(calculatedGates);
	return calculatedGates;
}

GateTable TeamComposition::calculateGateTable(bool recalculateFromScratch, int timeoutSeconds, int maxTargetCompSize, int pruningMode, bool connectedChampsOnly) {
	return calculateGateTable(recalculateFromScratch, timeoutSeconds, maxTargetCompSize, pruningMode, TeamComposition(), connectedChampsOnly);
}

//Properly initializes static fields. Specifically:
//Creates an adjacency list of champions and other champions having the same traits given the set's current champs
//Copies traitData into currentSetTraits and copies champInfo into globalChampInfoMap
//Initializes champStringTo64BitMap, champ64BitToStringMap, traitStringToShortMap, and traitShortToStringMap with correct string-position pairs
void TeamComposition::initializeStatics(unordered_map<string, vector<int>> traitData, unordered_map<string, Champion> champInfo) {
	initialized = false;
	currentSetTraits = traitData;
	globalChampInfoMap = champInfo;

	vector<ChampionVariantData> variants;
	for (const auto& [championName, champion] : champInfo) {
		const auto& choices = champion.getInterchangeableTraits();
		if (choices.empty()) {
			variants.push_back({ championName, championName, champion.getWidth(), champion.getTraitMap() });
			continue;
		}

		for (const auto& choice : choices) {
			map<string, int> variantTraits = champion.getTraitMap();
			variantTraits[choice.first] += choice.second;
			variants.push_back({
				championName,
				getVariantLabel(championName, choice),
				champion.getWidth(),
				move(variantTraits)
			});
		}
	}
	if (variants.size() > 128) {
		throw runtime_error(
			"Champion choices expand to " + to_string(variants.size()) +
			" internal variants, but TeamComposition only supports up to 128."
		);
	}
	if (traitData.size() > 32) {
		throw runtime_error("TeamComposition only supports up to 32 traits.");
	}
	championVariantCount = (int)variants.size();

	championGraph.clear();
	championBitsetGraph.clear();
	champStringToBitPosMap.clear();
	champStringToBitsetMap.clear();
	traitStringToArrPosMap.clear();
	dragons.reset();
	scalescorns.reset();
	for (int i = 0; i < 128; ++i) {
		champBitPosToStringMap[i].clear();
		champWidthByBitPos[i] = 0;
		championConnectionsByBitPos[i].reset();
		championIdentityByBitPos[i].reset();
		champTraitDeltasByBitPos[i].clear();
	}
	for (int i = 0; i < 64; ++i) {
		traitArrPosToStringMap[i].clear();
	}

	// Give each trait a corresponding position in compTraits.
	int count = 0;
	for (const pair<string, vector<int>>& trait : traitData) {
		traitStringToArrPosMap.emplace(trait.first, count);
		++count;
	}
	for (const pair<const string, short>& trait : traitStringToArrPosMap) {
		traitArrPosToStringMap[(int)trait.second] = trait.first;
	}

	// Assign every internal variant a bit and build one identity mask per canonical champion.
	unordered_map<string, ChampSet> canonicalChampionMasks;
	for (int i = 0; i < championVariantCount; ++i) {
		const ChampionVariantData& variant = variants[i];
		if (!champStringToBitPosMap.emplace(variant.label, i).second) {
			throw runtime_error("Duplicate internal champion variant label: " + variant.label);
		}
		champBitPosToStringMap[i] = variant.label;
		champWidthByBitPos[i] = (short)variant.width;
		canonicalChampionMasks[variant.canonicalName].set(i);

		if (variant.traits.contains("Dragon")) dragons.set(i);
		if (variant.traits.contains("Scalescorn")) scalescorns.set(i);
		for (const auto& [traitName, traitValue] : variant.traits) {
			auto traitPosition = traitStringToArrPosMap.find(traitName);
			if (traitPosition == traitStringToArrPosMap.end()) {
				throw runtime_error(
					"Champion variant \"" + variant.label + "\" uses unknown trait \"" + traitName + "\"."
				);
			}
			champTraitDeltasByBitPos[i].push_back({ traitPosition->second, (short)traitValue });
		}
	}

	for (int i = 0; i < championVariantCount; ++i) {
		const ChampionVariantData& variant = variants[i];
		championIdentityByBitPos[i] = canonicalChampionMasks.at(variant.canonicalName);
		champStringToBitsetMap[variant.canonicalName] = championIdentityByBitPos[i];
		ChampSet exactVariant;
		exactVariant.set(i);
		champStringToBitsetMap[variant.label] = exactVariant;
	}

	// The public graph stays canonical: two source champions connect if any of
	// their fixed or interchangeable traits can match.
	unordered_map<string, map<string, int>> possibleTraitsByChampion;
	for (const auto& [championName, champion] : champInfo) {
		map<string, int> possibleTraits = champion.getTraitMap();
		for (const auto& [traitName, traitValue] : champion.getInterchangeableTraits()) {
			possibleTraits[traitName] += traitValue;
		}
		possibleTraitsByChampion.emplace(championName, move(possibleTraits));
	}
	for (const auto& [keyChampion, keyTraits] : possibleTraitsByChampion) {
		vector<string> connectedChamps; //records list of champions sharing a trait with keyChamp
		for (const auto& [otherChampion, otherTraits] : possibleTraitsByChampion) {
			if (otherChampion != keyChampion && sharesConnectingTrait(keyTraits, otherTraits)) {
				connectedChamps.push_back(otherChampion);
			}
		}
		championGraph.emplace(keyChampion, move(connectedChamps));
	}

	// Internal connections are variant-specific, so connected-only generation
	// follows only the trait selected for that pseudo champion.
	for (int i = 0; i < championVariantCount; ++i) {
		ChampSet connections;
		for (int j = 0; j < championVariantCount; ++j) {
			if (i == j || variants[i].canonicalName == variants[j].canonicalName) continue;
			if (sharesConnectingTrait(variants[i].traits, variants[j].traits)) connections.set(j);
		}
		championBitsetGraph.emplace(variants[i].label, connections);
		championConnectionsByBitPos[i] = connections;
	}

	//Set initialized to true. Once true, objects of TeamComposition can be constructed.
	initialized = true; 
}



/* OLD COMP FINDING ALGORITHM
* This was a recursive depth-first algorithm used before the algorithm with the gates.
//Assumes compositions are only made of unique champions
vector<TeamCompositionLite> TeamCompositionLite::getTeamCompList(int compSize){
	if (compSize < 1) throw runtime_error("TeamComposition::getTeamCompList must have parameter compSize > 0.");
	if (championGraph.size() < 1) throw runtime_error("No champions in champion graph.");
	unordered_set<TeamCompositionLite, teamCompLiteHash> compSet;
	for (pair<string,vector<string>> champ : championGraph) {
		int champWidth = globalChampInfoMap.at(champ.first).getWidth();
		if (compSize >= champWidth) addTeamComps(compSet, TeamCompositionLite(), champ.first, compSize - champWidth + 1);
	}
	vector<TeamCompositionLite> compList;
	for (const TeamCompositionLite& comp : compSet) {
		compList.push_back(comp);
	}
	return compList;
}
void TeamCompositionLite::addTeamComps(unordered_set<TeamCompositionLite, teamCompLiteHash>& compSet, TeamCompositionLite currentComp, string champ, int numChampsLeft) {
	TeamCompositionLite oldComp = currentComp;
	currentComp.addChamp(champ);
	if (oldComp!=currentComp) { //prunes unchanged comps and comps with both Dragon and Scalescorn champs
		if (numChampsLeft == 1) {
			compSet.emplace(currentComp);
		}
		else {
			for (string nextChamp : championGraph.at(champ)) {
				int nextChampWidth = globalChampInfoMap.at(nextChamp).getWidth();
				if (numChampsLeft > nextChampWidth) //prunes champs that are too 'wide' to be added
					addTeamComps(compSet, currentComp, nextChamp, numChampsLeft - nextChampWidth);
			}
		}
	}
}
*/
