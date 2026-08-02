#include "Champion.h"
#include "CompositionCache.h"
#include "CompBuilderUtils.h"
#include "TeamComposition.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace std;

namespace {
	bool expectTrue(const string& context, bool value) {
		if (value) return true;
		cerr << context << ": expected true" << endl;
		return false;
	}

	bool expectResolution(const string& context, CompositionResolution actual, CompositionResolution expected) {
		if (actual == expected) return true;
		cerr << context << ": expected " << compositionResolutionName(expected)
			<< ", got " << compositionResolutionName(actual) << endl;
		return false;
	}

	size_t regularFileCount(const filesystem::path& directory) {
		if (!filesystem::exists(directory)) return 0;
		size_t count = 0;
		for (const filesystem::directory_entry& entry : filesystem::directory_iterator(directory)) {
			if (entry.is_regular_file()) ++count;
		}
		return count;
	}
}

int main() {
	const filesystem::path testRoot = "tests\\.cache_regression";
	error_code cleanupError;
	filesystem::remove_all(testRoot, cleanupError);

	try {
		bool passed = true;
		unordered_map<string, Champion> champions;
		readChampInfo("tests\\fixtures\\champion_info_width.txt", champions);
		unordered_map<string, vector<int>> traits;
		readTraitInfo("tests\\fixtures\\trait_info_width.txt", traits);
		validateSetData(champions, traits);
		TeamComposition::initializeStatics(traits, champions);

		GatedCompositionRequest request;
		request.setNumber = "fixture";
		request.championInfoFile = "tests\\fixtures\\champion_info_width.txt";
		request.traitInfoFile = "tests\\fixtures\\trait_info_width.txt";
		request.compositionSize = 1;
		request.gateTimeoutSeconds = 1;
		request.cacheRoot = testRoot / "Cache";

		GatedCompositionResult first = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("initial cache miss calculates gates", first.resolution, CompositionResolution::CalculatedGates);
		passed &= expectTrue("initial calculation returns compositions", !first.compositions.empty());

		GatedCompositionResult exactHit = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("second request uses composition cache", exactHit.resolution, CompositionResolution::CompositionCacheHit);
		passed &= expectTrue("composition cache preserves count", exactHit.compositions.size() == first.compositions.size());

		filesystem::remove_all(request.cacheRoot / "compositions", cleanupError);
		GatedCompositionResult gateHit = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("missing result uses gate cache", gateHit.resolution, CompositionResolution::GateCacheHit);

		request.compositionSize = 2;
		GatedCompositionResult missingSize = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("missing size extends gate cache", missingSize.resolution, CompositionResolution::CalculatedGates);
		GatedCompositionResult cachedSize = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("extended size result is cached", cachedSize.resolution, CompositionResolution::CompositionCacheHit);

		size_t gatesBeforeEmblems = regularFileCount(request.cacheRoot / "gates");
		request.compositionSize = 1;
		request.emblemTraits = { "Beta" };
		GatedCompositionResult oneEmblem = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("single emblem has its own gate profile", oneEmblem.resolution, CompositionResolution::CalculatedGates);
		request.emblemTraits = { "Beta", "Beta" };
		GatedCompositionResult duplicateEmblems = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("duplicate emblems have multiset identity", duplicateEmblems.resolution, CompositionResolution::CalculatedGates);
		passed &= expectTrue(
			"single and duplicate emblems create distinct gate caches",
			regularFileCount(request.cacheRoot / "gates") == gatesBeforeEmblems + 2
		);
		request.emblemTraits = { "Beta", "Alpha" };
		GatedCompositionResult emblemOrderA = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("new emblem multiset calculates gates", emblemOrderA.resolution, CompositionResolution::CalculatedGates);
		request.emblemTraits = { "Alpha", "Beta" };
		GatedCompositionResult emblemOrderB = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("emblem order is canonicalized", emblemOrderB.resolution, CompositionResolution::CompositionCacheHit);

		filesystem::create_directories(testRoot);
		filesystem::path modifiedTraitInfo = testRoot / "TraitInfo_modified.txt";
		{
			ofstream output(modifiedTraitInfo);
			output << "Alpha 2\nBeta 1\n\n";
		}
		request.emblemTraits.clear();
		request.traitInfoFile = modifiedTraitInfo;
		GatedCompositionResult changedData = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("set data fingerprint invalidates cache", changedData.resolution, CompositionResolution::CalculatedGates);

		request.traitInfoFile = "tests\\fixtures\\trait_info_width.txt";
		request.gateTimeoutSeconds = 2;
		GatedCompositionResult changedTimeout = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("gate timeout has its own profile", changedTimeout.resolution, CompositionResolution::CalculatedGates);
		request.gateTimeoutSeconds = 1;
		request.connectedChampsOnly = true;
		GatedCompositionResult connectedOnly = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("connected-only mode has its own profile", connectedOnly.resolution, CompositionResolution::CalculatedGates);
		request.connectedChampsOnly = false;
		request.gateType = GateType::ActiveTraitTiers;
		GatedCompositionResult tierGates = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("gate type has its own profile", tierGates.resolution, CompositionResolution::CalculatedGates);
		request.refreshCache = true;
		GatedCompositionResult refreshed = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("refresh bypasses matching caches", refreshed.resolution, CompositionResolution::CalculatedGates);

		filesystem::remove_all(testRoot, cleanupError);
		return passed ? 0 : 1;
	}
	catch (const exception& error) {
		cerr << "Unexpected cache regression failure: " << error.what() << endl;
		filesystem::remove_all(testRoot, cleanupError);
		return 1;
	}
}
