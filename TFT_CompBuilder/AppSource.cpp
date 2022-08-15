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

int main() {
	
	unordered_map<string,Champion> set7Champs; //map of champion names and corresponding Champion object
	readChampInfo("Set7ChampionInfo.txt", set7Champs); //fills set7Champs with info from Set7ChampionInfo text file

	//Print list of Champions and their Traits
	cout << "Champions:" << endl;
	int count = 1;
	for (pair<string, Champion> champ : set7Champs) {
		cout << count << ". " << champ.second.toString() << endl;
		count++;
	}
	
	unordered_map<string, vector<int>> set7Traits; //map of trait names and their corresponding milestone values
	readTraitInfo("Set7TraitInfo.txt", set7Traits); //fills set7Traits with info from Set7TraitInfo text file

	//Print list of traits and their milestones
	cout << endl << "Traits:" << endl;
	count = 1;
	for (unordered_map<string, vector<int>>::const_iterator it = set7Traits.begin(); it != set7Traits.end(); ++it) {
		cout << count << ". " << it->first << " (";
		if (it->second.size() > 0) cout << it->second.at(0);
		for (int i = 1; i < it->second.size(); ++i) {
			cout << "/" << it->second.at(i);
		}
		cout << ")" << endl;
		++count;
	}

	TeamComposition::initializeStatics(set7Traits, set7Champs); //Uses data from the trait and champ maps to initialize TeamCompositions to be constructed and used
	unordered_map<string, vector<string>> championGraph = TeamComposition::getChampGraph(); 

	//Print championGraph map showing each champion's list of connected champs
	cout << endl << "Champion Graph:" << endl;
	count = 1;
	for (unordered_map<string, vector<string>>::const_iterator it = championGraph.begin(); it != championGraph.end(); ++it) {
		int numConnectedChamps = it->second.size();
		cout << count << ". " << it->first << ": [";
		if (numConnectedChamps > 0) {
			cout << it->second.at(0);
		}
		for (int i = 1; i < numConnectedChamps; ++i) {
			cout << ", " << it->second.at(i);
		}
		cout << "] - " << numConnectedChamps << " connected champions." << endl;
		++count;
	}

	//user input for settings and target comp size to be generated
	bool settings[3] = { 0,0,0 };
	cout << endl << "Do you want to change the default settings? (\"y\"/\"n\")." << endl;
	string ans;
	cin >> ans;
	if (ans == "y") {
		cout << "Enter the settings to generate the team compositions:" << endl;
		cout << "Enter 3 numbers, each either 0 or 1, separated by a space." << endl; 
		cout << "If the first number is 0, the trait gates are used, no gates if 1." << endl;
		cout << "If the second number is 0, unique active traits are considered, active trait tiers if 1." << endl;
		cout << "If the third number is 0, all champs are considered while building comps, only champions sharing a trait if 1." << endl;
		for (int i = 0; i < 3; ++i) {
			cin >> settings[i];
		}
	}
	
	int compositionSize = 1;
	cout << endl << "What size comp would you like to find? (Enter an integer between 1 and 9 (inclusive)): ";
	cin >> compositionSize;

	time_t programStartTime = time(NULL); //record time to measure algorithm runtime
	vector<TeamComposition> compList = TeamComposition::generateComps(compositionSize,settings); //comp generating algorithm
	
	//Print compList
	vector<TeamComposition> listToPrint = compList;
	cout << endl << "Generated " << listToPrint.size() << " team compositions of size " << compositionSize << ": " << endl;
	count = 1;
	for (const TeamComposition& comp : listToPrint) {
		cout << count << ":" << comp.toString() << endl;
		count++;
	}
	cout << "Total comps: " << listToPrint.size() << endl;

	cout << endl << "Program runtime: " << time(NULL) - programStartTime << " seconds." << endl;
	return 0;
}