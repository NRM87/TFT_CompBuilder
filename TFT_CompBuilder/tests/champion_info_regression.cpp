#include "Champion.h"
#include "CompBuilderUtils.h"
#include "TeamComposition.h"

#include <iostream>
#include <string>
#include <unordered_map>

using namespace std;

namespace {
	bool expectEqual(const string& context, int actual, int expected) {
		if (actual == expected) return true;
		cerr << context << ": expected " << expected << ", got " << actual << endl;
		return false;
	}

	int traitValue(const Champion& champion, const string& trait) {
		auto it = champion.getTraitMap().find(trait);
		return it == champion.getTraitMap().end() ? 0 : it->second;
	}

	bool containsGeneratedChampion(const vector<TeamComposition>& compositions, const string& champion) {
		for (const TeamComposition& composition : compositions) {
			if (composition.containsChamp(champion)) return true;
		}
		return false;
	}
}

int main() {
	try {
		bool passed = true;

		unordered_map<string, Champion> fixtureChampions;
		readChampInfo("tests\\fixtures\\champion_info_width.txt", fixtureChampions);
		const Champion& fixture = fixtureChampions.at("Test_Unit");
		passed &= expectEqual("generic width", fixture.getWidth(), 2);
		passed &= expectEqual("default width", fixtureChampions.at("Normal_Unit").getWidth(), 1);
		passed &= expectEqual("generic weighted trait", traitValue(fixture, "Alpha"), 2);
		passed &= expectEqual("generic ordinary trait", traitValue(fixture, "Beta"), 1);

		unordered_map<string, vector<int>> fixtureTraits = {
			{ "Alpha", { 2 } },
			{ "Beta", { 1 } }
		};
		TeamComposition::initializeStatics(fixtureTraits, fixtureChampions);
		int allCompsSettings[3] = { 1, 0, 0 };
		vector<TeamComposition> sizeOneComps = TeamComposition::generateComps(1, allCompsSettings);
		passed &= expectEqual("one-slot composition count", (int)sizeOneComps.size(), 1);
		passed &= expectEqual("default-width unit fits one slot", sizeOneComps.front().containsChamp("Normal_Unit"), 1);
		vector<TeamComposition> sizeTwoComps = TeamComposition::generateComps(2, allCompsSettings);
		passed &= expectEqual("two-slot composition count", (int)sizeTwoComps.size(), 1);
		if (!sizeTwoComps.empty()) {
			passed &= expectEqual("width-two unit fills two slots", sizeTwoComps.front().containsChamp("Test_Unit"), 1);
			passed &= expectEqual("generated composition width", sizeTwoComps.front().size(), 2);
		}
		GateTable fixtureGates{};
		fixtureGates.activeTraitGates[1][0] = 3;
		fixtureGates.activeTraitGates[1][1] = 2;
		TeamComposition::setGateTable(fixtureGates);
		int gatedSettings[3] = { 0, 0, 0 };
		vector<TeamComposition> gatedSizeTwoComps = TeamComposition::generateComps(2, gatedSettings);
		passed &= expectEqual("width-two unit is pruned at its occupied width", (int)gatedSizeTwoComps.size(), 1);
		if (!gatedSizeTwoComps.empty()) {
			passed &= expectEqual("width-two unit bypasses earlier-width gate", gatedSizeTwoComps.front().containsChamp("Test_Unit"), 1);
		}

		unordered_map<string, Champion> set7Champions;
		readChampInfo("SetInfos\\Set7\\ChampionInfo.txt", set7Champions);
		unordered_map<string, vector<int>> set7Traits;
		readTraitInfo("SetInfos\\Set7\\TraitInfo.txt", set7Traits);
		try {
			validateSetData(set7Champions, set7Traits);
		}
		catch (const exception& ex) {
			cerr << "Set 7 validation failed: " << ex.what() << endl;
			passed = false;
		}
		const Champion& aoShin = set7Champions.at("Ao_Shin");
		passed &= expectEqual("Set 7 dragon width", aoShin.getWidth(), 2);
		passed &= expectEqual("Set 7 enhanced origin", traitValue(aoShin, "Tempest"), 3);
		passed &= expectEqual("Set 7 Dragon trait", traitValue(aoShin, "Dragon"), 1);
		TeamComposition::initializeStatics(set7Traits, set7Champions);
		vector<TeamComposition> set7SizeTwoComps = TeamComposition::generateComps(2, allCompsSettings);
		passed &= expectEqual("Set 7 dragon survives width-based generation", containsGeneratedChampion(set7SizeTwoComps, "Ao_Shin"), 1);

		unordered_map<string, Champion> set18Champions;
		readChampInfo("SetInfos\\Set18\\ChampionInfo.txt", set18Champions);
		unordered_map<string, vector<int>> set18Traits;
		readTraitInfo("SetInfos\\Set18\\TraitInfo.txt", set18Traits);
		try {
			validateSetData(set18Champions, set18Traits);
		}
		catch (const exception& ex) {
			cerr << "Set 18 validation failed: " << ex.what() << endl;
			passed = false;
		}
		const Champion& elderDragon = set18Champions.at("Elder_Dragon");
		passed &= expectEqual("Set 18 Elder Dragon width", elderDragon.getWidth(), 2);
		passed &= expectEqual("Set 18 Riftbeast contribution", traitValue(elderDragon, "Riftbeast"), 2);
		TeamComposition::initializeStatics(set18Traits, set18Champions);
		vector<TeamComposition> set18SizeTwoComps = TeamComposition::generateComps(2, allCompsSettings);
		passed &= expectEqual("Set 18 Elder Dragon survives width-based generation", containsGeneratedChampion(set18SizeTwoComps, "Elder_Dragon"), 1);

		return passed ? 0 : 1;
	}
	catch (const exception& ex) {
		cerr << "Unexpected regression-test failure: " << ex.what() << endl;
		return 1;
	}
}
