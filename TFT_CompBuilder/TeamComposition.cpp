#include "TeamComposition.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <limits>
using namespace std;

bool TeamComposition::initialized = false;
bool TeamComposition::gateTableInitialized = false;
ChampSet TeamComposition::dragons = 0;
ChampSet TeamComposition::scalescorns = 0;
unordered_map<string, Champion> TeamComposition::globalChampInfoMap;
unordered_map<string, vector<int>> TeamComposition::currentSetTraits;
unordered_map<string, vector<string>> TeamComposition::championGraph;
unordered_map<string, ChampSet> TeamComposition::championBitsetGraph;
unordered_map<string, int> TeamComposition::champStringToBitPosMap;
string TeamComposition::champBitPosToStringMap[128];
string TeamComposition::traitArrPosToStringMap[64];
unordered_map<string, short> TeamComposition::traitStringToArrPosMap;
short TeamComposition::champWidthByBitPos[128];
ChampSet TeamComposition::championConnectionsByBitPos[128];
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
	auto it = champStringToBitPosMap.find(champion);
	if (it == champStringToBitPosMap.end()) return false;
	return champions.test(it->second);
}

//Returns a string representation of the comp
string TeamComposition::toString() const {
	string s = "";
	int champCount = 0;
	for (int i = 0; i < globalChampInfoMap.size(); ++i) {
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
	return addChamp(champStringToBitPosMap.at(champ));
}

bool TeamComposition::addChamp(int champBitPos) {
	if (champions.test(champBitPos)) return false;
	champions.set(champBitPos); //Add the champ to the comp, duplicates are not added

	for (const TraitDelta& trait : champTraitDeltasByBitPos[champBitPos]) {
		compTraits[trait.traitPos] += trait.traitValue;
	}

	connectedChamps |= championConnectionsByBitPos[champBitPos]; //adds champ's connected champs to the comp's connected champs
	connectedChamps &= (~champions); //removes champs from connectedChamps that are already in the comp

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

TeamComposition::CompSet TeamComposition::buildNextCompSet(const CompSet& compSet, int targetCompSize, const int settings[3], int gateBound, int prevTraitValMax, int& currTraitValMax, double& elapsedSeconds, double timeoutSeconds, bool* timedOut) {
	const int champCount = (int)globalChampInfoMap.size();
	CompSet nextCompSet;
	currTraitValMax = 0;
	auto iterationStart = std::chrono::steady_clock::now();
	if (timedOut) *timedOut = false;

	for (const TeamComposition& currComp : compSet) {
		ChampSet connections;
		if (settings[2] && currComp.size() > 0) connections = currComp.connectedChamps; //only consider champs that share traits with the current comp's champs
		else connections = ~currComp.champions; //consider every champ not in the current comp already

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

			if (!settings[0]) {
				int traitValue = (settings[1] == 1 ? nextComp.getActiveTraitTiersTotal() : nextComp.getActiveTraitsTotal());
				if (traitValue > currTraitValMax) currTraitValMax = traitValue;
				if (settings[1] == 2) {
					if (traitValue < prevTraitValMax + 1) continue;
				}
				else if (traitValue < gateBound) {
					continue;
				}
			}

			nextCompSet.emplace(nextComp);
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
		CompSet nextCompSet = buildNextCompSet(compSet, compSize, settings, gateBound, prevTraitValMax, currTraitValMax, elapsedSeconds);
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

GateTable TeamComposition::calculateGateTable(bool recalculateFromScratch, int timeoutSeconds, int maxTargetCompSize, int pruningMode, bool connectedChampsOnly) {
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

	for (int targetCompSize = 1; targetCompSize <= maxTargetCompSize; ++targetCompSize) {
		CompSet compSet;
		compSet.emplace(TeamComposition());
		int prevTraitValMax = 0;
		int scratchStartGate = 0;
		if (recalculateFromScratch && targetCompSize > 1) {
			scratchStartGate = (pruningMode == 1)
				? calculatedGates.activeTierGates[targetCompSize - 2][targetCompSize - 2]
				: calculatedGates.activeTraitGates[targetCompSize - 2][targetCompSize - 2];
		}
		int previousAcceptedGate = scratchStartGate;

		for (int iterationCompSize = 1; iterationCompSize <= targetCompSize; ++iterationCompSize) {
			int settings[3] = { 0, pruningMode, connectedChampsOnly ? 1 : 0 };
			int gateBound = recalculateFromScratch
				? previousAcceptedGate
				: max(0, pruningMode == 1
					? currentGateTable.activeTierGates[targetCompSize - 1][iterationCompSize - 1]
					: currentGateTable.activeTraitGates[targetCompSize - 1][iterationCompSize - 1]);
			int highestSlowBound = -1;
			int lowestEmptyBound = std::numeric_limits<int>::max();
			int weakestSuccessGate = -1;
			int weakestSuccessTraitValue = 0;
			CompSet weakestSuccessCompSet;

			while (true) {
				int currentLayerMaxTraitValue = 0;
				double currentLayerElapsedSeconds = 0.0;
				CompSet nextCompSet = buildNextCompSet(compSet, targetCompSize, settings, gateBound, prevTraitValMax, currentLayerMaxTraitValue, currentLayerElapsedSeconds);
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
						gateBound = highestSlowBound + max(1, (weakestSuccessGate - highestSlowBound) / 2);
						continue;
					}

					if (gateBound == 0) {
						throw runtime_error(
							"Unable to find a non-empty gate for pruning mode " + to_string(pruningMode) +
							", target size " + to_string(targetCompSize) +
							", iteration size " + to_string(iterationCompSize) + "."
						);
					}
					gateBound = max(0, gateBound - 1);
					continue;
				}

				if (iterationCompSize < targetCompSize) {
					int probeMaxTraitValue = 0;
					double probeElapsedSeconds = 0.0;
					bool timedOut = false;
					CompSet probeCompSet = buildNextCompSet(nextCompSet, targetCompSize, settings, 0, currentLayerMaxTraitValue, probeMaxTraitValue, probeElapsedSeconds, (double)timeoutSeconds, &timedOut);
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
							gateBound = highestSlowBound + max(1, (weakestSuccessGate - highestSlowBound) / 2);
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
							gateBound = highestSlowBound + max(1, (weakestSuccessGate - highestSlowBound) / 2);
							continue;
						}

						if (gateBound == 0) {
							throw runtime_error(
								"Unable to find a gate that preserves a non-empty next iteration for pruning mode " + to_string(pruningMode) +
								", target size " + to_string(targetCompSize) +
								", iteration size " + to_string(iterationCompSize) + "."
							);
						}
						gateBound = max(0, gateBound - 1);
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

				if (weakestSuccessGate == 0 || highestSlowBound + 1 >= weakestSuccessGate) {
					int acceptedGate = (iterationCompSize == targetCompSize) ? weakestSuccessTraitValue : weakestSuccessGate;
					if (pruningMode == 1) calculatedGates.activeTierGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
					else calculatedGates.activeTraitGates[targetCompSize - 1][iterationCompSize - 1] = acceptedGate;
					compSet.swap(weakestSuccessCompSet);
					prevTraitValMax = weakestSuccessTraitValue;
					previousAcceptedGate = weakestSuccessGate;
					break;
				}

				gateBound = (highestSlowBound >= 0)
					? highestSlowBound + max(1, (weakestSuccessGate - highestSlowBound) / 2)
					: weakestSuccessGate - 1;
			}
		}
	}

	setGateTable(calculatedGates);
	return calculatedGates;
}

//Properly initializes static fields. Specifically:
//Creates an adjacency list of champions and other champions having the same traits given the set's current champs
//Copies traitData into currentSetTraits and copies champInfo into globalChampInfoMap
//Initializes champStringTo64BitMap, champ64BitToStringMap, traitStringToShortMap, and traitShortToStringMap with correct string-position pairs
void TeamComposition::initializeStatics(unordered_map<string, vector<int>> traitData, unordered_map<string, Champion> champInfo) {
	currentSetTraits = traitData;
	globalChampInfoMap = champInfo;
	championGraph.clear();
	championBitsetGraph.clear();
	champStringToBitPosMap.clear();
	traitStringToArrPosMap.clear();
	dragons.reset();
	scalescorns.reset();
	for (int i = 0; i < 128; ++i) {
		champBitPosToStringMap[i].clear();
		champWidthByBitPos[i] = 0;
		championConnectionsByBitPos[i].reset();
		champTraitDeltasByBitPos[i].clear();
	}
	for (int i = 0; i < 64; ++i) {
		traitArrPosToStringMap[i].clear();
	}

	//64bit long-longs represent comps; each bit corresponds to a different champion, 1 if it is in the comp and 0 if not
	//Set champStringToBitPosMap, along with dragons and scalescorns lists (which are long-longs)
	int count = 0;
	for (const pair<string, Champion>& champ : champInfo) {
		//Maps a champion's name as a string to a number. The number is the bit position in a long-long that the champ will now correspond with
		champStringToBitPosMap.emplace(champ.first, count); 

		//Adds champions with certain traits to respective bitsets that keep track of which champions hold these traits 
		if (champ.second.getTraitMap().count("Dragon")) dragons.set(count); 
		if (champ.second.getTraitMap().count("Scalescorn")) scalescorns.set(count);

		++count; //increment so that next champion will have a different corresponding number for mapping
	}
	//Set corresponding champBitPosToStringMap which just maps the champion's name as a string to the number determined in the previous loop above.
	for (const pair<string, int>& champ : champStringToBitPosMap) {
		champBitPosToStringMap[champ.second] = champ.first;
	}

	//Give each trait a corresponding number (which is an array position/index) and then initialize the subsequent trait string-to-arrayposition and arrayposition-to-string maps
	count = 0;
	for (const pair<string, vector<int>>& trait : traitData) {
		traitStringToArrPosMap.emplace(trait.first, count);
		++count;
	}
	for (const pair<const string, short>& trait : traitStringToArrPosMap) {
		traitArrPosToStringMap[(int)trait.second] = trait.first;
	}

	for (const pair<string, Champion>& champ : champInfo) {
		int champBitPos = champStringToBitPosMap.at(champ.first);
		champWidthByBitPos[champBitPos] = (short)champ.second.getWidth();
		for (const pair<string, int>& trait : champ.second.getTraitMap()) {
			champTraitDeltasByBitPos[champBitPos].push_back(
				{ traitStringToArrPosMap.at(trait.first), (short)trait.second }
			);
		}
	}

	//Initializes the champion adjacency list map
	//First two nested loops go through each permutation of two champions with the first champion being keyChamp and the second being otherChamp
	for (const pair<string, Champion>& keyChamp : champInfo) {
		vector<string> connectedChamps; //records list of champions sharing a trait with keyChamp
		for (const pair<string, Champion>& otherChamp : champInfo) {
			if (otherChamp.first != keyChamp.first) {
				//Last two nested loops compare each trait between keyChamp and otherChamp
				for (const pair<string, int>& keyChampTrait : keyChamp.second.getTraitMap()) {
					for (const pair<string, int>& otherChampTrait : otherChamp.second.getTraitMap()) {
						//if trait names match, otherChamp's name is added to the list of keyChamp's connected champs. (Threats must not connect to eachother)
						if (keyChampTrait.first == otherChampTrait.first && keyChampTrait.first != "Threat") {
							connectedChamps.push_back(otherChamp.first); 
							goto nextChampComparison; //once champs are found to share a trait, skip comparing other traits
						}
					}
				}
			}
		nextChampComparison:; //Go here when skipping trait finding loops due to a shared trait already being found.
		}
		championGraph.emplace(keyChamp.first, connectedChamps); //add keyChamp and its connectedChamp list to the champGraph map
	}

	//Initializes another champion adjacency list, instead using a long-long to store each list
	for (const pair<string, vector<string>>& champConnections : championGraph) {
		ChampSet connections;
		for (const string& champ : champConnections.second) {
			connections.set(champStringToBitPosMap.at(champ));
		}
		championBitsetGraph.emplace(champConnections.first, connections);
		championConnectionsByBitPos[champStringToBitPosMap.at(champConnections.first)] = connections;
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
