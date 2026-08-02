#pragma once
#include "Champion.h"
#include <array>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
using std::string;
using std::vector;

class TeamComposition;

struct GateTable {
	static constexpr int SIZE = 10;
	std::array<std::array<int, SIZE>, SIZE> activeTraitGates{};
	std::array<std::array<int, SIZE>, SIZE> activeTierGates{};
};

// Functions for reading files with trait and champion information into maps.
// Champion tokens support Trait:N for an N-point trait contribution, @width=N for board width,
// and [TraitA|TraitB:N] for one interchangeable trait choice per generated champion variant.
void readChampInfo(string fileName, unordered_map<string, Champion>& champions);
void readTraitInfo(string fileName, unordered_map<string, vector<int>>& traits);
void validateSetData(const unordered_map<string, Champion>& champions, const unordered_map<string, vector<int>>& traits);

//Function for parsing cdragon patch json into trait and champion text files
void parseCDragon(string fileName, string setNum);

//Specific filters for certain sets:

void set12TahmFilter(vector<TeamComposition>& listToFilter);
