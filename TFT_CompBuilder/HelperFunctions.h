#pragma once
#include "Champion.h"
#include "TeamComposition.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
using namespace std;

//Functions for reading files with trait and champion information into maps
void readChampInfo(string fileName, unordered_map<string, Champion>& champions);
void readTraitInfo(string fileName, unordered_map<string, vector<int>>& traits);

//Function for parsing cdragon patch json into trait and champion text files
void parseCDragon(string fileName, string setNum);