#pragma once
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <bitset>
#include "Champion.h"
using namespace std;
using ChampSet = bitset<128>;

//TeamCompositions are essentially sets of Champions with infrastructure to keep track of stats relevant to the Teamfight Tactics game.
//Internally, the set is a 128 bitset where each bit corresponds to a different champion, determined in initalizeStatics. 
//Because of this, static fields which help enforce the corresponding bit positions and champions should be first initialized before comp objects can be used.
class TeamComposition {
public:
	//Constructor, only works once static fields have been initialized in initializeStatics
	TeamComposition() : compSize(0), champions(0), connectedChamps(0)
	{
		if (!initialized) throw runtime_error("TeamComposition statics not initialized.");
		for (int i = 0; i < currentSetTraits.size(); ++i) {
			compTraits[i] = 0;
		}
	}

	//Accessors
	short size() const { return compSize; }; 
	int getActiveTraitTiersTotal() const; 
	int getActiveTraitsTotal() const;
	bool containsChamp(const string& champion) const;
	string toString() const; 

	//Mutators
	bool addChamp(string champ); //adds a champ to the comp and updates relavant trait and connections fields
	
	//Operators
	bool operator==(const TeamComposition& left) const { return this->champions == left.champions; };

	//Static accessors
	static vector<TeamComposition> generateComps(int compSize, int traitSettings[3]); //returns a list of comps based on a target comp size and settings
	static vector<TeamComposition> generateComps(int compSize); //calls default settings of generateComps
	static unordered_map<string, vector<string>> getChampGraph() { return championGraph; }; 

	//Static mutators
	//Initializes the static variables based on a trait-traitMilestone map and a name-Champion map
	static void initializeStatics(unordered_map<string, vector<int>> traitData, unordered_map<string, Champion> champInfo); 
private: 
	//Hash function for TeamCompositionLite objects, uses internal bitset champions for the hash.
	struct teamCompHash {
		size_t operator()(const TeamComposition& comp) const {
			ChampSet champs = comp.champions;
			return hash<ChampSet>()(champs);
		}
	};

	//Object fields
	//There shouldn't be more than 32 traits, and values for compSize and any compTraits[n] should never exceed 128 based on typical Teamfight Tactics numbers
	short compSize; //total width of champions in the comp
	short compTraits[32]; //each trait and its active amount for the comp
	bitset<128> champions; //64bit set of champions in the comp
	bitset<128> connectedChamps; //64bit set of champions not already in the comp that share traits with any of the champions in the comp

	static bool initialized; //keeps track of if static fields have been properly initialized
	static const int MAX_COMP_SIZE = 10; //maximum comp size for generateComps algorithm

	//gates for pruning incremental sizes of comps during generateComps algorithm
	static const int ACTIVE_TRAIT_GATES[MAX_COMP_SIZE][MAX_COMP_SIZE]; 
	static const int ACTIVE_TIER_GATES[MAX_COMP_SIZE][MAX_COMP_SIZE];

	//Sets of champions that have a certain trait. In 64bit form to easily operate with comp objects.
	static ChampSet dragons; 
	static ChampSet scalescorns;

	//Static fields for champion and trait information.
	static unordered_map<string, Champion> globalChampInfoMap; //map of champ name to Champion object
	static unordered_map<string, vector<string>> championGraph; //map of champ to set of champs sharing a trait
	static unordered_map<string, ChampSet> championBitsetGraph; //map of champ to 64bit set of champs sharing a trait
	static unordered_map<string, vector<int>> currentSetTraits; //map of traits to their trait milestones

	//Static fields for converting trait or champion strings to their corresponding positions in arrays or 64bit sets, respectively
	static unordered_map<string, int> champStringToBitPosMap;
	static string champBitPosToStringMap[];
	static unordered_map<string, short> traitStringToArrPosMap;
	static string traitArrPosToStringMap[];

	/* OLD COMP-GENERATING ALGORITHM FUNCTIONS
	static vector<TeamCompositionLite> getTeamCompList(int compSize); 
	static void addTeamComps(unordered_set<TeamCompositionLite, teamCompLiteHash>& compList, TeamCompositionLite currentComp, string champ, int numChampsLeft);
	*/
};
