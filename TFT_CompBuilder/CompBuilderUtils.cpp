#include "CompBuilderUtils.h"
#include "TeamComposition.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <unordered_set>
#include "json.hpp"
using namespace std;
using json = nlohmann::json;

#define SETINFODIR(set) ("SetInfos\\Set" + set)
#define CHAMPINFOFILE(set) (SETINFODIR(set) + "\\ChampionInfo.txt")
#define TRAITINFOFILE(set) (SETINFODIR(set) + "\\TraitInfo.txt")

namespace {
	int parsePositiveChampionValue(const string& value, const string& token, const string& championName) {
		size_t parsedCharacters = 0;
		int parsedValue = 0;
		try {
			parsedValue = stoi(value, &parsedCharacters);
		}
		catch (const exception&) {
			throw runtime_error(
				"Invalid champion info token \"" + token + "\" for \"" + championName + "\". Expected a positive integer."
			);
		}

		if (parsedCharacters != value.size() || parsedValue < 1) {
			throw runtime_error(
				"Invalid champion info token \"" + token + "\" for \"" + championName + "\". Expected a positive integer."
			);
		}
		return parsedValue;
	}

	pair<string, int> parseChampionTraitToken(const string& token, const string& championName) {
		string traitName = token;
		int traitValue = 1;
		size_t valueSeparator = token.rfind(':');
		if (valueSeparator != string::npos) {
			traitName = token.substr(0, valueSeparator);
			if (traitName.empty()) {
				throw runtime_error("Invalid empty trait name in champion info token \"" + token + "\" for \"" + championName + "\".");
			}
			traitValue = parsePositiveChampionValue(
				token.substr(valueSeparator + 1),
				token,
				championName
			);
		}
		return { traitName, traitValue };
	}

}

//Read champion information from text file into a map
void readChampInfo(string fileName, unordered_map<string, Champion>& champions) {
	ifstream champInfo(fileName);
	if (!champInfo.is_open()) {
		throw runtime_error("Could Not Open Champion File");
	}

	string championLine;
	while (getline(champInfo, championLine)) {
		istringstream line(championLine);
		string championName;
		if (!(line >> championName)) continue;

		Champion champ(championName);
		string token;
		while (line >> token) {
			static const string WIDTH_PREFIX = "@width=";
			if (token.starts_with(WIDTH_PREFIX)) {
				champ.setWidth(parsePositiveChampionValue(
					token.substr(WIDTH_PREFIX.size()),
					token,
					championName
				));
				continue;
			}

			bool startsChoiceGroup = token.starts_with('[');
			bool endsChoiceGroup = token.ends_with(']');
			if (startsChoiceGroup || endsChoiceGroup) {
				if (!startsChoiceGroup || !endsChoiceGroup) {
					throw runtime_error("Invalid interchangeable trait list \"" + token + "\" for \"" + championName + "\". Expected [TraitA|TraitB].");
				}
				if (!champ.getInterchangeableTraits().empty()) {
					throw runtime_error("Champion \"" + championName + "\" has more than one interchangeable trait list.");
				}

				string choices = token.substr(1, token.size() - 2);
				istringstream choiceStream(choices);
				string choiceToken;
				unordered_set<string> choiceNames;
				while (getline(choiceStream, choiceToken, '|')) {
					if (choiceToken.empty()) {
						throw runtime_error("Champion \"" + championName + "\" has an empty interchangeable trait choice in \"" + token + "\".");
					}
					auto [traitName, traitValue] = parseChampionTraitToken(choiceToken, championName);
					if (!choiceNames.emplace(traitName).second) {
						throw runtime_error("Champion \"" + championName + "\" repeats interchangeable trait \"" + traitName + "\".");
					}
					champ.addInterchangeableTrait(traitName, traitValue);
				}
				if (champ.getInterchangeableTraits().size() < 2) {
					throw runtime_error("Champion \"" + championName + "\" must have at least two interchangeable trait choices.");
				}
				continue;
			}

			auto [traitName, traitValue] = parseChampionTraitToken(token, championName);
			champ.addTrait(traitName, traitValue);
		}

		if (!champions.emplace(champ.getName(), champ).second) {
			throw runtime_error("Champion already exists while reading champion info file: " + championName);
		}
	}
}

//Read trait information from text file into a map
void readTraitInfo(string fileName, unordered_map<string, vector<int>>& traits) {
	ifstream traitInfo(fileName);
	if (!traitInfo.is_open()) {
		throw runtime_error("Could Not Open Trait File");
	}

	string traitLine;
	while (getline(traitInfo, traitLine)) {
		istringstream line(traitLine);
		string traitName;
		if (!(line >> traitName)) continue;

		vector<int> traitMilestones;
		int traitMilestone = 0;
		while (line >> traitMilestone) {
			traitMilestones.push_back(traitMilestone);
		}
		if (!line.eof()) {
			throw runtime_error("Invalid trait milestone while reading trait info for \"" + traitName + "\".");
		}
		if (traitMilestones.empty()) throw runtime_error("Trait milestone not found while reading trait info file.");
		if (!traits.emplace(traitName, traitMilestones).second) {
			throw runtime_error("Trait already exists while reading trait info file: " + traitName);
		}
	}
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

	size_t expandedChampionCount = 0;
	for (const auto& [champName, champion] : champions) {
		expandedChampionCount += max<size_t>(1, champion.getInterchangeableTraits().size());
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

		const auto& interchangeableTraits = champion.getInterchangeableTraits();
		if (interchangeableTraits.size() == 1) {
			issues.push_back("Champion \"" + champName + "\" has only one interchangeable trait choice.");
		}
		unordered_set<string> choiceNames;
		for (const auto& [traitName, traitValue] : interchangeableTraits) {
			if (traitName.empty()) {
				issues.push_back("Champion \"" + champName + "\" has an empty interchangeable trait name.");
				continue;
			}
			if (!choiceNames.emplace(traitName).second) {
				issues.push_back("Champion \"" + champName + "\" repeats interchangeable trait \"" + traitName + "\".");
			}
			if (!traits.contains(traitName)) {
				issues.push_back(
					"Champion \"" + champName + "\" uses interchangeable trait \"" + traitName + "\" but that trait is missing from the trait info file."
				);
			}
			if (traitValue <= 0) {
				issues.push_back(
					"Champion \"" + champName + "\" has non-positive value " + to_string(traitValue) + " for interchangeable trait \"" + traitName + "\"."
				);
			}
		}
	}
	if (expandedChampionCount > 128) {
		issues.push_back(
			"Loaded champion choices expand to " + to_string(expandedChampionCount) +
			" internal variants, but TeamComposition only supports up to 128."
		);
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

