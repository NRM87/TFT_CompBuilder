#include "Champion.h"
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace std;

//Constructor for the typical three trait set-up of most champions in Teamfight Tactics
Champion::Champion(string champName, string champOrigin, string champClass, string thirdTrait) : traitTotal(0), width(1), name(champName)  {
	this->traits.emplace(champOrigin,1);
	this->traits.emplace(champClass,1);
	this->traitTotal += 2;
	if (thirdTrait != "") {
		this->traits.emplace(thirdTrait, 1);
		++this->traitTotal;
	}
}

//Constructor for a champion with a large list of traits
Champion::Champion(string champName, const vector<string>& traits) : traitTotal(traits.size()), width(1), name(champName) {
	for (int i = 0; i < traits.size(); ++i) {
		this->addTrait(traits.at(i));
	}
}

//Adds trait to champ if not added already and increases its value by an amount
void Champion::addTrait(string trait, int traitValue) {
	//try to place trait with given amount; if it already exists, instead add amount to trait
	if (!this->traits.emplace(trait,traitValue).second) this->traits.at(trait) += traitValue; 
	this->traitTotal += traitValue;
}

void Champion::addInterchangeableTrait(string trait, int traitValue) {
	this->interchangeableTraits.emplace_back(trait, traitValue);
}

//Returns a string representation of the champion and its traits.
string Champion::toString() const {
	ostringstream stream;
	stream << left << setw(16) << (this->name + ": ");
	for (map<string, int>::const_iterator it = traits.begin(); it != traits.end(); ++it) {
		stream << (it->first + "(" + std::to_string(it->second) + ") ");
	}
	if (!interchangeableTraits.empty()) {
		stream << "[";
		for (size_t i = 0; i < interchangeableTraits.size(); ++i) {
			if (i > 0) stream << "|";
			stream << interchangeableTraits[i].first << "(" << interchangeableTraits[i].second << ")";
		}
		stream << "]";
	}
	string s = stream.str();
	return s;
}
