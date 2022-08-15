#include "HelperFunctions.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace std;

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




