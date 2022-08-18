#include "TeamComposition.h"
#include <stdexcept>
#include <iostream>
using namespace std;

//Construct static fields:
//Gates determine how many traits or trait tiers (depending on settings) a comp needs at each size during their building in generateComps
//For runtime efficiency, gates eliminate generated comps that do not have enough traits or tiers to reach the target amount of the last gate of the target size.
//The values of the gates were determined experimentally, with them being the max value that will still ensure all comps of the last gate size will make it to the end.
const int TeamComposition::ACTIVE_TRAIT_GATES[MAX_COMP_SIZE][MAX_COMP_SIZE] = { //gates for when counting based on getActiveTraitsTotal
	{2,0,0,0,0,0,0,0,0},
	{2,3,0,0,0,0,0,0,0},
	{2,3,5,0,0,0,0,0,0},
	{2,3,5,6,0,0,0,0,0},
	{2,3,4,5,7,0,0,0,0},
	{2,3,4,5,6,8,0,0,0},
	{2,3,3,4,6,7,9,0,0},
	{1,2,3,4,5,7,8,10,0},
	{1,2,3,4,5,6,8,9,11} };
const int TeamComposition::ACTIVE_TIER_GATES[MAX_COMP_SIZE][MAX_COMP_SIZE] = { //gates for when counting based on getActiveTraitTiersTotal
	{2,0,0,0,0,0,0,0,0},
	{2,3,0,0,0,0,0,0,0},
	{2,3,5,0,0,0,0,0,0},
	{2,3,4,6,0,0,0,0,0},
	{2,3,4,5,7,0,0,0,0},
	{2,3,4,5,7,9,0,0,0},
	{2,3,4,5,7,8,10,0,0},
	{2,3,4,5,7,9,10,12,0},
	{2,3,4,5,7,9,10,11,13} };
bool TeamComposition::initialized = false;
long long TeamComposition::dragons = 0;
long long TeamComposition::scalescorns = 0;
unordered_map<string, Champion> TeamComposition::globalChampInfoMap;
unordered_map<string, vector<int>> TeamComposition::currentSetTraits;
unordered_map<string, vector<string>> TeamComposition::championGraph;
unordered_map<string, long long> TeamComposition::championLLGraph;
unordered_map<string, int> TeamComposition::champStringToBitPosMap;
string TeamComposition::champBitPosToStringMap[64];
string TeamComposition::traitArrPosToStringMap[32];
unordered_map<string, short> TeamComposition::traitStringToArrPosMap;

//Returns the amount of active trait tiers
int TeamComposition::getActiveTraitTiersTotal() const {
	if (currentSetTraits.size() == 0) throw runtime_error("Current Set Traits not initialized.");
	int total = 0;
	for (int i = 0; i < currentSetTraits.size(); ++i) {
		string trait = traitArrPosToStringMap[i];
		int traitVal = compTraits[i];
		vector<int> traitMilestones(currentSetTraits.at(trait));
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
		if (traitVal >= currentSetTraits.at(trait).at(0)) ++total; 																					
	}
	return total;
}

//Returns a string representation of the comp
string TeamComposition::toString() const {
	string s = "";
	int champCount = 0;
	for (int i = 0; i < globalChampInfoMap.size(); ++i) {
		if ((champions & (1LL << i)) >> i) {
			s += " " + champBitPosToStringMap[i];
			++champCount;
			if (champCount == MAX_COMP_SIZE) return s;
		}
	}
	return s;
}

//Adds a champ to the comp and updates compTraits and connectedChamps accordingly. Returns true if champ was added.
bool TeamComposition::addChamp(string champ) { 
	long long oldChamps = champions;
	champions |= (1LL << champStringToBitPosMap.at(champ)); //Add the champ to the comp, duplicates are not added
	if (champions == oldChamps) return false; //if the champ was already in the comp, do nothing

	map<string, int> champTraits = globalChampInfoMap.at(champ).getTraitMap();
	for (pair<string, int> trait : champTraits) {
		string traitName = trait.first;
		int traitValue = trait.second;
		compTraits[traitStringToArrPosMap.at(traitName)] += (short)traitValue;
	}
		
	connectedChamps |= championLLGraph.at(champ); //adds champ's connected champs to the comp's connected champs
	connectedChamps &= (~champions); //removes champs from connectedChamps that are already in the comp
		
	compSize += globalChampInfoMap.at(champ).getWidth(); //increases the comp's size by the champ's width
	return true;
}

//Generates and returns a list of comps given a target comp size and list of settings
//settings[] guide: settings[0] - use gates or not; settings[1] - use trait or tier gates; settings[2] - use all champs or only connectedChamps
//Example settings: Default - {0,0,0} | All possible comps - {1,0,0} | Use tier gates, not trait gates - {0,1,0}
vector<TeamComposition> TeamComposition::generateComps(int compSize, bool settings[3]) {
	if (compSize > 9 || compSize < 1) throw runtime_error("generateBestComps9 must have parameter compSize between 1 and 9 (inclusive).");

	//Pick what gates to use based on settings[1]
	const int(*GATES)[9][9];
	if (settings[1]) GATES = &ACTIVE_TIER_GATES;
	else GATES = &ACTIVE_TRAIT_GATES;

	int currCompSize = 0;
	unordered_set<TeamComposition, teamCompHash> compSet; //holds the previous while loop iteration's generated comps
	compSet.emplace(TeamComposition());
	unordered_set<TeamComposition, teamCompHash> nextCompSet; //holds newly generated comps for the next while loop iteration

	while (currCompSize < compSize) { 
		++currCompSize;

		for (const TeamComposition& currComp : compSet) { 
			if (currComp.compSize < currCompSize) {
				//Pick what champs will be considered when generating the next-sized comps
				long long connections;
				if (settings[2] && currComp.size() > 0) connections = currComp.connectedChamps; //only consider champs that share traits with the current comp's champs
				else connections = ~currComp.champions; //consider every champ not in the current comp already

				//iterates for each champ that could be added to the comp
				for (int i = 0; i < globalChampInfoMap.size(); ++i) { 
					string champ = champBitPosToStringMap[i];

					bool champConnected = (connections & (1LL << i)) >> i; //if champ is supposed to be considered
					bool champFits = globalChampInfoMap.at(champ).getWidth() <= (compSize - currComp.compSize);
					if (!champConnected || !champFits) continue; 

					//Generates a new comp from a comp generated from last while loop iteration and a new champ that passes the above if-statement
					TeamComposition nextComp(currComp);
					nextComp.addChamp(champ);

					long long nextCompDragons = (dragons & nextComp.champions);
					bool hasAtMostOneDragon = !((nextCompDragons & (nextCompDragons - 1)) && nextCompDragons);
					bool hasDragonAndScalescorn = (dragons & nextComp.champions) && (scalescorns & nextComp.champions);
					bool hasSufficientTraits = true;
					if (!settings[0]) {
						int traitValue = (settings[1] ? nextComp.getActiveTraitTiersTotal() : nextComp.getActiveTraitsTotal());
						hasSufficientTraits = (traitValue >= (*GATES)[compSize - 1][nextComp.compSize - 1]);
					}
					if (!hasSufficientTraits || !hasAtMostOneDragon || hasDragonAndScalescorn) continue;

					//If the generated comp passes the above if-statement, send it to the next round of building and pruning.
					nextCompSet.emplace(nextComp);
				}
			}
			//saves the comp for the next iteration of the while loop if it was too big for this iteration but smaller than the final target comp size
			else if (currComp.compSize <= compSize) {
				nextCompSet.emplace(currComp); 
			}
		}
		//sets compSet to nextCompSet so the process can be repeated in the next iteration of the while loop
		compSet = nextCompSet; 
		nextCompSet.clear();
	}

	//copy final set of comps to a list
	vector<TeamComposition> compList;
	for (const TeamComposition &comp : compSet) {
		compList.push_back(comp);
	}

	return compList;
}
//Calls generateComps with default settings
vector<TeamComposition> TeamComposition::generateComps(int compSize) {
	bool traitSettings[3] = { 0,0,0 };
	return generateComps(compSize, traitSettings);
}

//Properly initializes static fields. Specifically:
//Creates an adjacency list of champions and other champions having the same traits given the set's current champs
//Copies traitData into currentSetTraits and copies champInfo into globalChampInfoMap
//Initializes champStringTo64BitMap, champ64BitToStringMap, traitStringToShortMap, and traitShortToStringMap with correct string-position pairs
void TeamComposition::initializeStatics(unordered_map<string, vector<int>> traitData, unordered_map<string, Champion> champInfo) {
	currentSetTraits = traitData;
	globalChampInfoMap = champInfo;

	//64bit long-longs represent comps; each bit corresponds to a different champion, 1 if it is in the comp and 0 if not
	//Set champStringToBitPosMap, along with dragons and scalescorns lists (which are long-longs)
	int count = 0;
	for (const pair<string, Champion>& champ : champInfo) {
		//Maps a champion's name as a string to a number. The number is the bit position in a long-long that the champ will now correspond with
		champStringToBitPosMap.emplace(champ.first, count); 

		//Adds champions with certain traits to respective long-longs that keep track of which champions hold these traits 
		if (champ.second.getTraitMap().count("Dragon")) dragons += (1LL << count); 
		if (champ.second.getTraitMap().count("Scalescorn")) scalescorns += (1LL << count);

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
	for (const pair<string, char>& trait : traitStringToArrPosMap) {
		traitArrPosToStringMap[(int)trait.second] = trait.first;
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
						//if trait names match, otherChamp's name is added to the list of keyChamp's connected champs. (*Dragons must not connect to eachother)
						if (keyChampTrait.first == otherChampTrait.first && keyChampTrait.first != "Dragon") {
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
	for (pair<string, vector<string>> champConnections : championGraph) {
		long long connections = 0LL;
		for (string champ : champConnections.second) {
			connections |= (1LL << champStringToBitPosMap.at(champ));
		}
		championLLGraph.emplace(champConnections.first, connections);
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
