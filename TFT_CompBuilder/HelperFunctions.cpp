#include "HelperFunctions.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "json.hpp"
using namespace std;
using json = nlohmann::json;

#define CHAMPINFOFILE(set) ("Set" + set + "ChampionInfo.txt")
#define TRAITINFOFILE(set) ("Set" + set + "TraitInfo.txt")

//TODO: add protection against traits not matching between champion and trait infos

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
			if (holder == "Dragon") {
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
		traits.emplace(holder, traitMilestones);
	}
	line.clear();
	traitInfo.close();
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



