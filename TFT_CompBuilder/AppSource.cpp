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
#include <chrono>
#include "Champion.h"
#include "CompBuilderUtils.h"
#include "TeamComposition.h"
using namespace std;

#define SET "17"
#define CDRAGONJSON(set) ("TFTSetJSONs\\set" + set + ".json")
#define LEGACYGUARD (stoi(SET) >= 8 && SET != "8.5")
#define SETINFODIR(set) ("SetInfos\\Set" + set)
#define CHAMPINFOFILE(set) (SETINFODIR(set) + "\\ChampionInfo.txt")
#define TRAITINFOFILE(set) (SETINFODIR(set) + "\\TraitInfo.txt")
#define USINGINPUTFILE 1 //1 if using "SourceInput.txt" as input; 0 if using std::cin
#define DEFAULT_GATE_TIMEOUT_SECONDS 10

int main() {
	try {
		string set = SET;
		ifstream input("SourceInput.txt");
		istream& in = USINGINPUTFILE ? static_cast<istream&>(input) : static_cast<istream&>(cin);
		auto readToken = [&](const string& context) -> string {
			string value;
			if (!(in >> value)) {
				throw runtime_error("Failed reading " + context + (USINGINPUTFILE ? " from SourceInput.txt." : " from standard input."));
			}
			return value;
		};
		auto readInt = [&](const string& context) -> int {
			int value = 0;
			if (!(in >> value)) {
				throw runtime_error("Failed reading integer for " + context + (USINGINPUTFILE ? " from SourceInput.txt." : " from standard input."));
			}
			return value;
		};

		cout << "Welcome to the Set " << SET << " team composition generator!" << endl;
		cout << "Would you like to update the current set information by parsing the CDragon JSON (\"y\"/\"n\")? (If info text files are not up to date.)" << endl;
		string ans = readToken("set update choice");
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
		validateSetData(setChamps, setTraits);
		TeamComposition::setGateTable(readGateTable(set));

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

		cout << endl << "Initializing statistics..." << endl;
		TeamComposition::initializeStatics(setTraits, setChamps); //Uses data from the trait and champ maps to initialize TeamCompositions to be constructed and used
		cout << "Getting champ graph..." << endl;
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


		//user input for settings and target comp size to be generated
		int settings[3] = { 0,0,0 };
		cout << endl << "Do you want to change the default settings? (\"y\"/\"n\")." << endl;
		ans = readToken("settings change choice");
		if (ans == "y") {
			cout << "Enter the settings to generate the team compositions:" << endl;
			cout << "Enter 3 numbers, each either 0 or 1, separated by a space." << endl; 
			cout << "If the first number is 0, the trait gates are used, no gates if 1." << endl;
			cout << "If the second number is 0, unique active traits are considered, active trait tiers if 1." << endl;
			cout << "If the third number is 0, all champs are considered while building comps, only champions sharing a trait if 1." << endl;
			for (int i = 0; i < 3; ++i) {
				settings[i] = readInt("settings[" + to_string(i) + "]");
				if (settings[i] != 0 && settings[i] != 1) {
					throw runtime_error("Invalid value for settings[" + to_string(i) + "]. Expected 0 or 1.");
				}
			}
		}
		
		int compositionSize = 1;
		cout << endl << "What size comp would you like to find? (Enter an integer between 1 and 10 (inclusive)): ";
		compositionSize = readInt("composition size");
		if (compositionSize < 1 || compositionSize > 10) {
			throw runtime_error("Composition size must be between 1 and 10.");
		}

		cout << endl << "Do you want to recalculate this set's gate file up to the selected comp size and pruning mode? (\"y\"/\"n\")." << endl;
		ans = readToken("gate recalculation choice");
		if (ans == "y") {
			if (settings[1] != 0 && settings[1] != 1) {
				throw runtime_error("Gate recalculation requires pruning mode 0 (trait gates) or 1 (tier gates).");
			}

			cout << "Should gate recalculation start from scratch instead of the current gate file? (\"y\"/\"n\")." << endl;
			string recalcFromScratchAnswer = readToken("recalculate-from-scratch choice");
			bool recalculateFromScratch = (recalcFromScratchAnswer == "y");

			int gateTimeoutSeconds = DEFAULT_GATE_TIMEOUT_SECONDS;
			cout << "Enter the gate recalculation timeout per iteration in seconds (default " << DEFAULT_GATE_TIMEOUT_SECONDS << "): " << endl;
			gateTimeoutSeconds = readInt("gate recalculation timeout");
			if (gateTimeoutSeconds < 1) {
				throw runtime_error("Gate recalculation timeout must be at least 1 second.");
			}

			cout << "Recalculating gate file..." << endl;
			auto gateRecalculationStart = std::chrono::steady_clock::now();
			GateTable recalculatedGates = TeamComposition::calculateGateTable(
				recalculateFromScratch,
				gateTimeoutSeconds,
				compositionSize,
				settings[1],
				settings[2] != 0
			);
			writeGateTable(set, recalculatedGates);
			double gateRecalculationSeconds = std::chrono::duration<double>(
				std::chrono::steady_clock::now() - gateRecalculationStart
			).count();
			cout << "Gate recalculation complete. Elapsed time: " << gateRecalculationSeconds << " seconds." << endl;
		}

		cout << endl << "Generating team compositions...";

		time_t programStartTime = time(NULL); //record time to measure algorithm runtime
		vector<TeamComposition> compList = TeamComposition::generateComps(compositionSize,settings); //comp generating algorithm
		
		

		//Print compList
		vector<TeamComposition> listToPrint = compList;
		
		if (SET == "12") set12TahmFilter(listToPrint);

		cout << endl << "Generated " << listToPrint.size() << " team compositions of size " << compositionSize << ": " << endl;
		count = 1;
		for (const TeamComposition& comp : listToPrint) {
			cout << setw(2) << count << ":" << comp.toString() << " | Num traits: " << comp.getActiveTraitsTotal() << endl;
			count++;
		}
		cout << "Total comps: " << listToPrint.size() << endl;

		cout << endl << "Program runtime: " << time(NULL) - programStartTime << " seconds." << endl;
		return 0;
	}
	catch (const exception& ex) {
		cerr << endl << "Runtime error: " << ex.what() << endl;
		return 1;
	}
}
