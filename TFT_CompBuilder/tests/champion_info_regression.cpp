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

	int countGeneratedChampion(const vector<TeamComposition>& compositions, const string& champion) {
		int count = 0;
		for (const TeamComposition& composition : compositions) {
			if (composition.containsChamp(champion)) ++count;
		}
		return count;
	}

	const TeamComposition* findComposition(const vector<TeamComposition>& compositions, const string& text) {
		for (const TeamComposition& composition : compositions) {
			if (composition.toString().find(text) != string::npos) return &composition;
		}
		return nullptr;
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

		unordered_map<string, Champion> variantChampions;
		readChampInfo("tests\\fixtures\\champion_info_variants.txt", variantChampions);
		passed &= expectEqual("canonical interchangeable-trait champion count", (int)variantChampions.size(), 3);
		unordered_map<string, vector<int>> variantTraits = {
			{ "Base", { 1 } },
			{ "Alpha", { 1 } },
			{ "Gamma", { 2 } }
		};
		validateSetData(variantChampions, variantTraits);
		TeamComposition::initializeStatics(variantTraits, variantChampions);
		TeamComposition explicitVariantComp;
		passed &= expectEqual("first explicit interchangeable-trait variant can be added", explicitVariantComp.addChamp("Adaptive_Unit[Alpha]"), 1);
		passed &= expectEqual("second variant of the same champion is rejected", explicitVariantComp.addChamp("Adaptive_Unit[Gamma:2]"), 0);
		passed &= expectEqual("rejected sibling variant does not consume a slot", explicitVariantComp.size(), 1);
		vector<TeamComposition> variantSizeOneComps = TeamComposition::generateComps(1, allCompsSettings);
		passed &= expectEqual("expanded interchangeable-trait composition count", (int)variantSizeOneComps.size(), 4);
		passed &= expectEqual("canonical champion matches every internal variant", countGeneratedChampion(variantSizeOneComps, "Adaptive_Unit"), 2);
		const TeamComposition* alphaVariant = findComposition(variantSizeOneComps, "Adaptive_Unit[Alpha]");
		const TeamComposition* gammaVariant = findComposition(variantSizeOneComps, "Adaptive_Unit[Gamma:2]");
		passed &= expectEqual("Alpha variant is labeled", alphaVariant != nullptr, 1);
		passed &= expectEqual("weighted Gamma variant is labeled", gammaVariant != nullptr, 1);
		if (alphaVariant != nullptr) passed &= expectEqual("Alpha variant active traits", alphaVariant->getActiveTraitsTotal(), 2);
		if (gammaVariant != nullptr) passed &= expectEqual("weighted Gamma variant active traits", gammaVariant->getActiveTraitsTotal(), 2);

		vector<TeamComposition> variantSizeTwoComps = TeamComposition::generateComps(2, allCompsSettings);
		passed &= expectEqual("mutually exclusive interchangeable-trait combinations", (int)variantSizeTwoComps.size(), 5);
		for (const TeamComposition& composition : variantSizeTwoComps) {
			size_t firstAdaptive = composition.toString().find("Adaptive_Unit[");
			if (firstAdaptive != string::npos) {
				passed &= expectEqual(
					"composition contains only one interchangeable-trait variant",
					composition.toString().find("Adaptive_Unit[", firstAdaptive + 1) == string::npos,
					1
				);
			}
		}

		int connectedSettings[3] = { 1, 0, 1 };
		vector<TeamComposition> connectedVariantComps = TeamComposition::generateComps(2, connectedSettings);
		passed &= expectEqual("variant-specific connected composition count", (int)connectedVariantComps.size(), 2);
		passed &= expectEqual("Alpha variant connects to Alpha ally", findComposition(connectedVariantComps, "Adaptive_Unit[Alpha]") != nullptr, 1);
		passed &= expectEqual("Gamma variant connects to Gamma ally", findComposition(connectedVariantComps, "Adaptive_Unit[Gamma:2]") != nullptr, 1);

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
		passed &= expectEqual("Set 18 canonical champion count", (int)set18Champions.size(), 65);
		const Champion& elderDragon = set18Champions.at("Elder_Dragon");
		passed &= expectEqual("Set 18 Elder Dragon width", elderDragon.getWidth(), 2);
		passed &= expectEqual("Set 18 Riftbeast contribution", traitValue(elderDragon, "Riftbeast"), 2);
		passed &= expectEqual("Set 18 Lux interchangeable origin count", (int)set18Champions.at("Lux").getInterchangeableTraits().size(), 9);
		TeamComposition::initializeStatics(set18Traits, set18Champions);
		vector<TeamComposition> set18SizeOneComps = TeamComposition::generateComps(1, allCompsSettings);
		passed &= expectEqual("Set 18 one-slot internal composition count", (int)set18SizeOneComps.size(), 72);
		passed &= expectEqual("Set 18 Lux internal variant count", countGeneratedChampion(set18SizeOneComps, "Lux"), 9);
		vector<TeamComposition> set18SizeTwoComps = TeamComposition::generateComps(2, allCompsSettings);
		passed &= expectEqual("Set 18 Elder Dragon survives width-based generation", containsGeneratedChampion(set18SizeTwoComps, "Elder_Dragon"), 1);

		return passed ? 0 : 1;
	}
	catch (const exception& ex) {
		cerr << "Unexpected regression-test failure: " << ex.what() << endl;
		return 1;
	}
}
