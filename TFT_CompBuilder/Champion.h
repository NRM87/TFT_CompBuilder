#pragma once
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
#include <fstream>
using namespace std;

//Champions are objects in the game Teamfight Tactics that have traits and take up space with their "width".
//*Some stuff here is not used currently but is present for possible future projects and consistency with how champions are presented in the game.
class Champion{
public:
	//Constructors
	Champion() : traitTotal(0), width(1), name("") {}; //default, constructs "empty" champion
	Champion(string champName) : traitTotal(0), width(1), name(champName) {}; //constructs a champion with a name but no traits
	Champion(string champName, string champClass, string champOrigin, string thirdTrait = ""); //Constructor for the typical three trait set-up of most champs in Teamfight Tactics
	Champion(string champName, const vector<string>& traits); //Constructor for a champion with a large list of traits

	//Accessors
	int getTraitTotal() const { return traitTotal; };
	int getWidth() const { return width; };
	string getName() const { return name; };
	map<string, int> getTraitMap() const { return traits; };
	string toString() const;

	//Mutators
	void addTrait(string trait, int traitValue = 1); 
	void setName(string name) { this->name = name; };
	void setWidth(int width) { this->width = width; };
private:
	//int cost;
	int traitTotal;
	int width; //amount of "space" the champion takes up, usually just 1
	string name;
	map<string, int> traits;
};



