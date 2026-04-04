#include "CompBuilderUtils.h"
#include "TeamComposition.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <algorithm>
#include "json.hpp"
using namespace std;
using json = nlohmann::json;

#define SETINFODIR(set) ("SetInfos\\Set" + set)
#define CHAMPINFOFILE(set) (SETINFODIR(set) + "\\ChampionInfo.txt")
#define TRAITINFOFILE(set) (SETINFODIR(set) + "\\TraitInfo.txt")
#define GATEINFOFILE(set) (SETINFODIR(set) + "\\Gates.json")
#define TEMPGATEINFOFILE(set) (SETINFODIR(set) + "\\Gates.json.tmp")

namespace {
	string getEmblemGateKey(const vector<string>& emblemTraits) {
		if (emblemTraits.empty()) return "";

		vector<string> normalizedTraits = emblemTraits;
		sort(normalizedTraits.begin(), normalizedTraits.end());

		ostringstream key;
		for (size_t i = 0; i < normalizedTraits.size(); ++i) {
			if (i > 0) key << "__";
			key << normalizedTraits[i];
		}
		return key.str();
	}

	string getGateDirectory(const string& setNum, const vector<string>& emblemTraits) {
		if (emblemTraits.empty()) return SETINFODIR(setNum);
		return SETINFODIR(setNum) + "\\EmblemGates";
	}

	string getGateFilePath(const string& setNum, const vector<string>& emblemTraits) {
		if (emblemTraits.empty()) return GATEINFOFILE(setNum);
		return getGateDirectory(setNum, emblemTraits) + "\\" + getEmblemGateKey(emblemTraits) + ".json";
	}

	string getTempGateFilePath(const string& setNum, const vector<string>& emblemTraits) {
		if (emblemTraits.empty()) return TEMPGATEINFOFILE(setNum);
		return getGateDirectory(setNum, emblemTraits) + "\\" + getEmblemGateKey(emblemTraits) + ".json.tmp";
	}
}

//Read champion information from text file into a map
void readChampInfo(string fileName, unordered_map<string, Champion>& champions) {
	ifstream champInfo;
	champInfo.open(fileName);
	if (!champInfo.is_open()) {
		throw runtime_error("Could Not Open Champion File");
	}
	istringstream line;
	while (!champInfo.eof()) {
		string holder;
		getline(champInfo, holder); //holder now holds the next line, a champ and its traits
		line.clear();
		line.str(holder);
		line >> holder; //holder now holds the first string in the line, the champ's name
		Champion champ(holder);
		while (!line.eof()) {
			line >> holder; //holder now holds the next string in the line, a trait
			champ.addTrait(holder);
			if (holder == "Dragon77") {
				line >> holder; //reads dragon-enhanced trait, which should come right after "Dragon" in the text file
				champ.addTrait(holder, 2);
				champ.setWidth(2);
			}
		}
		champions.emplace(champ.getName(), champ);
	}
	line.clear();
	champInfo.close();
}

//Read trait information from text file into a map
void readTraitInfo(string fileName, unordered_map<string, vector<int>>& traits) {
	ifstream traitInfo;
	traitInfo.open(fileName);
	if (!traitInfo.is_open()) {
		throw runtime_error("Could Not Open Trait File");
	}
	istringstream line;
	while (!traitInfo.eof()) {
		string holder;
		getline(traitInfo, holder); //holder now holds the next line, a trait and its milestones
		line.clear();
		line.str(holder);
		line >> holder; //holder now holds the first string in the line, the trait's name
		vector<int> traitMilestones;
		while (!line.eof()) {
			int traitMilestone;
			line >> traitMilestone;
			traitMilestones.push_back(traitMilestone);
		}
		if (traitMilestones.size() == 0) throw runtime_error("Trait milestone not found while reading trait info file.");
		if (traits.contains(holder)) throw runtime_error("Trait already exists while reading trait info file.");
		traits.emplace(holder, traitMilestones);
	}
	line.clear();
	traitInfo.close();
}

void validateSetData(const unordered_map<string, Champion>& champions, const unordered_map<string, vector<int>>& traits) {
	vector<string> issues;

	if (champions.empty()) issues.push_back("No champions were loaded from the champion info file.");
	if (traits.empty()) issues.push_back("No traits were loaded from the trait info file.");
	if (champions.size() > 128) {
		issues.push_back("Loaded " + to_string(champions.size()) + " champions, but TeamComposition only supports up to 128.");
	}
	if (traits.size() > 32) {
		issues.push_back("Loaded " + to_string(traits.size()) + " traits, but TeamComposition only supports up to 32.");
	}

	for (const auto& [champName, champion] : champions) {
		if (champName.empty()) {
			issues.push_back("Found a champion entry with an empty name.");
			continue;
		}

		for (const auto& [traitName, traitValue] : champion.getTraitMap()) {
			if (traitName.empty()) {
				issues.push_back("Champion \"" + champName + "\" has an empty trait name.");
				continue;
			}
			if (!traits.contains(traitName)) {
				issues.push_back(
					"Champion \"" + champName + "\" uses trait \"" + traitName + "\" but that trait is missing from the trait info file."
				);
			}
			if (traitValue <= 0) {
				issues.push_back(
					"Champion \"" + champName + "\" has non-positive value " + to_string(traitValue) + " for trait \"" + traitName + "\"."
				);
			}
		}
	}

	for (const auto& [traitName, milestones] : traits) {
		if (traitName.empty()) {
			issues.push_back("Found a trait entry with an empty name.");
			continue;
		}
		if (milestones.empty()) {
			issues.push_back("Trait \"" + traitName + "\" has no milestones.");
			continue;
		}
		for (int milestone : milestones) {
			if (milestone <= 0) {
				issues.push_back("Trait \"" + traitName + "\" has non-positive milestone " + to_string(milestone) + ".");
			}
		}
		if (!is_sorted(milestones.begin(), milestones.end())) {
			issues.push_back("Trait \"" + traitName + "\" has milestones that are not sorted in ascending order.");
		}
	}

	if (!issues.empty()) {
		ostringstream message;
		message << "Set data validation failed. Fix the champion/trait info files before generating compositions.";
		int issuesToPrint = min((int)issues.size(), 8);
		for (int i = 0; i < issuesToPrint; ++i) {
			message << "\n- " << issues[i];
		}
		if (issues.size() > issuesToPrint) {
			message << "\n- ...and " << (issues.size() - issuesToPrint) << " more issue(s).";
		}
		throw runtime_error(message.str());
	}
}

GateTable readGateTable(const string& setNum, const vector<string>& emblemTraits) {
	string gateFilePath = getGateFilePath(setNum, emblemTraits);
	if (!emblemTraits.empty() && !std::filesystem::exists(gateFilePath)) {
		gateFilePath = getGateFilePath(setNum, {});
	}

	ifstream gateInfo(gateFilePath);
	if (!gateInfo.is_open()) {
		throw runtime_error("Could Not Open Gate File: " + gateFilePath);
	}

	json gateJson = json::parse(gateInfo);
	GateTable gates;
	gates.activeTraitGates = gateJson.at("active_trait_gates").get<decltype(gates.activeTraitGates)>();
	gates.activeTierGates = gateJson.at("active_tier_gates").get<decltype(gates.activeTierGates)>();
	return gates;
}

void writeGateTable(const string& setNum, const GateTable& gates, const vector<string>& emblemTraits) {
	std::filesystem::create_directories(getGateDirectory(setNum, emblemTraits));
	string gateFileName = getGateFilePath(setNum, emblemTraits);
	string tempGateFileName = getTempGateFilePath(setNum, emblemTraits);
	ofstream gateInfo(tempGateFileName);
	if (!gateInfo.is_open()) {
		throw runtime_error("Could Not Open Gate Temp File For Writing: " + tempGateFileName);
	}

	gateInfo << "{\n";
	gateInfo << "  \"generator_version\": 1,\n";
	gateInfo << "  \"active_trait_gates\": [\n";
	for (int row = 0; row < GateTable::SIZE; ++row) {
		gateInfo << "    [";
		for (int col = 0; col < GateTable::SIZE; ++col) {
			if (col > 0) gateInfo << ", ";
			gateInfo << gates.activeTraitGates[row][col];
		}
		gateInfo << "]";
		if (row + 1 < GateTable::SIZE) gateInfo << ",";
		gateInfo << "\n";
	}
	gateInfo << "  ],\n";
	gateInfo << "  \"active_tier_gates\": [\n";
	for (int row = 0; row < GateTable::SIZE; ++row) {
		gateInfo << "    [";
		for (int col = 0; col < GateTable::SIZE; ++col) {
			if (col > 0) gateInfo << ", ";
			gateInfo << gates.activeTierGates[row][col];
		}
		gateInfo << "]";
		if (row + 1 < GateTable::SIZE) gateInfo << ",";
		gateInfo << "\n";
	}
	gateInfo << "  ]\n";
	gateInfo << "}\n";
	gateInfo.close();

	std::error_code fsError;
	std::filesystem::rename(tempGateFileName, gateFileName, fsError);
	if (fsError) {
		std::filesystem::remove(gateFileName, fsError);
		fsError.clear();
		std::filesystem::rename(tempGateFileName, gateFileName, fsError);
		if (fsError) {
			throw runtime_error("Could Not Replace Gate File: " + gateFileName + " (" + fsError.message() + ")");
		}
	}
}

string underscore(string str) {
	for (int i = 0; i < str.size(); ++i) {
		if (str.at(i) == ' ') str.at(i) = '_';
	}
	return str;
}

void parseCDragon(string fileName, string setNum) {

	//open json file
	ifstream jsonfile;
	jsonfile.open(fileName);
	cout << fileName << endl;
	if (!jsonfile.is_open()) {
		throw runtime_error("Could not open set " + setNum + " cdragon json file:" + fileName);
		return;
	}

	json data = json::parse(jsonfile);
	json champArray = data["sets"][setNum]["champions"];
	json traitArray = data["sets"][setNum]["traits"];
	std::filesystem::create_directories(SETINFODIR(setNum));
	ofstream outC(CHAMPINFOFILE(setNum));
	ofstream outT(TRAITINFOFILE(setNum));

	//parse champion info
	if (champArray.size() > 0) {
		outC << underscore(champArray[0]["name"]);
		for (auto trait : champArray[0]["traits"]) {
			outC << " " << underscore(trait);
		}
	}
	for (int i = 1; i < champArray.size(); ++i) {
		if (champArray[i]["traits"].size() == 0) continue;
		outC << endl << underscore(champArray[i]["name"]);
		json traits = champArray[i]["traits"];
		for (auto trait : traits) {
			outC << " " << underscore(trait);
		}
	}

	//parse trait info
	if (traitArray.size() > 0) {
		outT << underscore(traitArray[0]["name"]);
		for (auto effect : traitArray[0]["effects"]) {
			outT << " " << effect["minUnits"];
		}
	}
	for (int i = 1; i < traitArray.size(); ++i) {
		outT << endl << underscore(traitArray[i]["name"]);
		json milestones = traitArray[i]["effects"];
		for (auto effect : milestones) {
			outT << " " << effect["minUnits"];
		}
	}
}


//
//
//
void set12TahmFilter(vector<TeamComposition>& listToFilter) {
	std::erase_if(listToFilter, [](TeamComposition t) {return !t.containsChamp("Tahm_Kench");});
}

