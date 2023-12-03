#include <string>
#include <vector>
#include <iostream>
#include <iomanip> 
#include <fstream>
#include <sstream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <ctime>
#include "Champion.h"
#include "HelperFunctions.h"
#include "TeamComposition.h"
using namespace std;

#define SET "8.5"
#define CDRAGONJSON(set) ("TFTSetJSONs\\set" + set + ".json")
#define LEGACYGUARD (stoi(SET) >= 8 && SET != "8.5")
#define CHAMPINFOFILE(set) ("Set" + set + "ChampionInfo.txt")
#define TRAITINFOFILE(set) ("Set" + set + "TraitInfo.txt")
#define USINGINPUTFILE 1 //1 if using "SourceInput.txt" as input; 0 if using std::cin

int main() {
	string set = SET;
	ifstream input("SourceInput.txt");

	cout << "Welcome to the Set " << SET << " team composition generator!" << endl;
	cout << "Would you like to update the current set information (\"y\"/\"n\")? (If info text files are not up to date.)" << endl;
	string ans;
	USINGINPUTFILE ? input >> ans : cin >> ans;
	if (ans == "y" && LEGACYGUARD) {
		cout << "Parsing cdragon json..." << endl;
		parseCDragon(CDRAGONJSON(set), SET);
		cout << "CDragon parsing complete." << endl;
	}

	//Initialize Champions
	unordered_map<string,Champion> setChamps; //map of champion names and corresponding Champion object
	readChampInfo(CHAMPINFOFILE(set), setChamps); //fills setChamps with info from SetChampionInfo text file

	//Print list of Champions and their Traits
	cout << "Champions:" << endl;
	int count = 1;
	for (pair<string, Champion> champ : setChamps) {
		cout << setw(2) << count << ". " << champ.second.toString() << endl;
		count++;
	}
	
	//Initialize Traits
	unordered_map<string, vector<int>> setTraits; //map of trait names and their corresponding milestone values
	readTraitInfo(TRAITINFOFILE(set), setTraits); //fills setTraits with info from SetTraitInfo text file

	//Print list of traits and their milestones
	cout << endl << "Traits:" << endl;
	count = 1;
	for (unordered_map<string, vector<int>>::const_iterator it = setTraits.begin(); it != setTraits.end(); ++it) {
		cout << right << setw(2) << count << ". " << left << setw(15) << it->first << " (";
		if (it->second.size() > 0) cout << it->second.at(0);
		for (int i = 1; i < it->second.size(); ++i) {
			cout << "/" << it->second.at(i);
		}
		cout << ")" << endl;
		++count;
	}

	TeamComposition::initializeStatics(setTraits, setChamps); //Uses data from the trait and champ maps to initialize TeamCompositions to be constructed and used
	unordered_map<string, vector<string>> championGraph = TeamComposition::getChampGraph(); 

	//Print championGraph map showing each champion's list of connected champs
	cout << endl << "Champion Graph:" << endl;
	count = 1;
	for (unordered_map<string, vector<string>>::const_iterator it = championGraph.begin(); it != championGraph.end(); ++it) {
		int numConnectedChamps = it->second.size();
		cout << right << setw(2) << count << ". " << left << setw(16) << (it->first + ":");
		string temp = "[";
		if (numConnectedChamps > 0) {
			temp += it->second.at(0);
		}
		for (int i = 1; i < numConnectedChamps; ++i) {
			temp += (", " + it->second.at(i));
		}
		temp += "]";
		cout << left << setw(128) << temp << right << setw(2) << numConnectedChamps << " connected champions." << endl;
		++count;
	}

	//calculate new trait gates for algorithm (opitional)
	cout << endl << "Do you want to update algorithm trait gates?" << endl;
	//TODO: implement automatic trait gate updating


	//user input for settings and target comp size to be generated
	bool settings[3] = { 0,0,0 };
	cout << endl << "Do you want to change the default settings? (\"y\"/\"n\")." << endl;
	USINGINPUTFILE ? input >> ans : cin >> ans;
	if (ans == "y") {
		cout << "Enter the settings to generate the team compositions:" << endl;
		cout << "Enter 3 numbers, each either 0 or 1, separated by a space." << endl; 
		cout << "If the first number is 0, the trait gates are used, no gates if 1." << endl;
		cout << "If the second number is 0, unique active traits are considered, active trait tiers if 1." << endl;
		cout << "If the third number is 0, all champs are considered while building comps, only champions sharing a trait if 1." << endl;
		for (int i = 0; i < 3; ++i) {
			USINGINPUTFILE ? input >> settings[i] : cin >> settings[i];
		}
	}
	
	int compositionSize = 1;
	cout << endl << "What size comp would you like to find? (Enter an integer between 1 and 9 (inclusive)): ";
	USINGINPUTFILE ? input >> compositionSize : cin >> compositionSize;
	cout << endl << "Generating team compositions...";

	time_t programStartTime = time(NULL); //record time to measure algorithm runtime
	vector<TeamComposition> compList = TeamComposition::generateComps(compositionSize,settings); //comp generating algorithm
	
	//Print compList
	vector<TeamComposition> listToPrint = compList;
	cout << endl << "Generated " << listToPrint.size() << " team compositions of size " << compositionSize << ": " << endl;
	count = 1;
	for (const TeamComposition& comp : listToPrint) {
		cout << setw(2) << count << ":" << comp.toString() << endl;
		count++;
	}
	cout << "Total comps: " << listToPrint.size() << endl;

	cout << endl << "Program runtime: " << time(NULL) - programStartTime << " seconds." << endl;
	return 0;
}