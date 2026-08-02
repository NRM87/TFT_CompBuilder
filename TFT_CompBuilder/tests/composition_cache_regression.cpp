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

	filesystem::path firstRegularFile(const filesystem::path& directory) {
		if (!filesystem::exists(directory)) return {};
		for (const filesystem::directory_entry& entry : filesystem::directory_iterator(directory)) {
			if (entry.is_regular_file()) return entry.path();
		}
		return {};
	}

	void writeFile(const filesystem::path& path, const string& contents) {
		filesystem::create_directories(path.parent_path());
		ofstream output(path, ios::binary | ios::trunc);
		output << contents;
		if (!output) throw runtime_error("Could not write test file: " + path.string());
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

		CompositionCacheInventory initialInventory = inspectCompositionCache(request.cacheRoot);
		passed &= expectTrue("initial cache has one gate manifest", initialInventory.gateManifests == 1);
		passed &= expectTrue("initial cache has one composition manifest", initialInventory.compositionManifests == 1);
		passed &= expectTrue("initial cache uses immutable gate and composition objects",
			initialInventory.gateObjects == 1 && initialInventory.compositionObjects == 1);
		passed &= expectTrue("inventory reports cached composition count",
			initialInventory.cachedCompositions == first.compositions.size());

		filesystem::remove_all(request.cacheRoot / "v2" / "manifests" / "gates", cleanupError);
		filesystem::remove_all(request.cacheRoot / "v2" / "objects" / "gates", cleanupError);
		GatedCompositionResult exactWithoutGates = getOrCalculateGatedCompositions(request);
		passed &= expectResolution(
			"exact composition cache does not depend on a gate manifest or object",
			exactWithoutGates.resolution,
			CompositionResolution::CompositionCacheHit
		);

		request.refreshCache = true;
		GatedCompositionResult restored = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("refresh restores complete cache state", restored.resolution, CompositionResolution::CalculatedGates);
		request.refreshCache = false;

		filesystem::remove_all(request.cacheRoot / "v2" / "manifests" / "compositions", cleanupError);
		GatedCompositionResult gateHit = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("missing exact result uses gate cache", gateHit.resolution, CompositionResolution::GateCacheHit);

		filesystem::path compositionObject = firstRegularFile(request.cacheRoot / "v2" / "objects" / "compositions");
		passed &= expectTrue("composition object exists for corruption test", !compositionObject.empty());
		if (!compositionObject.empty()) writeFile(compositionObject, "corrupt\n");
		GatedCompositionResult recovered = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("corrupt exact object falls back to valid gates", recovered.resolution, CompositionResolution::GateCacheHit);
		GatedCompositionResult recoveredHit = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("corrupt exact object is repaired", recoveredHit.resolution, CompositionResolution::CompositionCacheHit);

		request.compositionSize = 2;
		GatedCompositionResult missingSize = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("missing size extends gate cache", missingSize.resolution, CompositionResolution::CalculatedGates);
		GatedCompositionResult cachedSize = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("extended size result is cached", cachedSize.resolution, CompositionResolution::CompositionCacheHit);

		uint64_t gatesBeforeEmblems = inspectCompositionCache(request.cacheRoot).gateManifests;
		request.compositionSize = 1;
		request.emblemTraits = { "Beta" };
		GatedCompositionResult oneEmblem = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("single emblem has its own gate profile", oneEmblem.resolution, CompositionResolution::CalculatedGates);
		request.emblemTraits = { "Beta", "Beta" };
		GatedCompositionResult duplicateEmblems = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("duplicate emblems have multiset identity", duplicateEmblems.resolution, CompositionResolution::CalculatedGates);
		passed &= expectTrue(
			"single and duplicate emblems create distinct gate manifests",
			inspectCompositionCache(request.cacheRoot).gateManifests == gatesBeforeEmblems + 2
		);
		request.emblemTraits = { "Beta", "Alpha" };
		GatedCompositionResult emblemOrderA = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("new emblem multiset calculates gates", emblemOrderA.resolution, CompositionResolution::CalculatedGates);
		request.emblemTraits = { "Alpha", "Beta" };
		GatedCompositionResult emblemOrderB = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("emblem order is canonicalized", emblemOrderB.resolution, CompositionResolution::CompositionCacheHit);

		filesystem::create_directories(testRoot);
		filesystem::path modifiedTraitInfo = testRoot / "TraitInfo_modified.txt";
		writeFile(modifiedTraitInfo, "Alpha 2\nBeta 1\n\n");
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
		request.refreshCache = false;

		writeFile(request.cacheRoot / "gates" / "legacy.json", "{}\n");
		writeFile(request.cacheRoot / "compositions" / "legacy.jsonl", "{}\n");
		writeFile(request.cacheRoot / "v2" / "staging" / "cache.tmp.test", "temporary\n");
		writeFile(request.cacheRoot / "v2" / "manifests" / "gates" / "invalid.json", "not json\n");
		writeFile(
			request.cacheRoot / "v2" / "objects" / "compositions" /
				"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.jsonl",
			"orphan\n"
		);
		CompositionCacheInventory dirtyInventory = inspectCompositionCache(request.cacheRoot);
		passed &= expectTrue("inventory finds legacy files", dirtyInventory.legacyFiles == 2);
		passed &= expectTrue("inventory finds a temporary file", dirtyInventory.temporaryFiles == 1);
		passed &= expectTrue("inventory finds an invalid manifest", dirtyInventory.invalidManifests == 1);
		passed &= expectTrue("inventory finds an orphan object", dirtyInventory.orphanedObjects >= 1);

		CompositionCachePruneResult pruned = pruneCompositionCache(request.cacheRoot);
		passed &= expectTrue("safe prune removes stale cache files", pruned.removedFiles >= 5);
		passed &= expectTrue("safe prune removes legacy files", pruned.after.legacyFiles == 0);
		passed &= expectTrue("safe prune removes temporary files", pruned.after.temporaryFiles == 0);
		passed &= expectTrue("safe prune removes invalid manifests", pruned.after.invalidManifests == 0);
		passed &= expectTrue("safe prune removes orphan objects", pruned.after.orphanedObjects == 0);

		CompositionCachePruneResult limited = pruneCompositionCache(request.cacheRoot, 0);
		passed &= expectTrue("zero-byte LRU limit evicts all cache files", limited.after.totalFiles == 0);

		filesystem::path blockedCacheRoot = testRoot / "blocked-cache-root";
		writeFile(blockedCacheRoot, "this regular file prevents creation of a cache directory\n");
		request.cacheRoot = blockedCacheRoot;
		request.gateType = GateType::ActiveTraits;
		GatedCompositionResult writeFailure = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("cache write failure is nonfatal", writeFailure.resolution, CompositionResolution::CalculatedGates);
		passed &= expectTrue("cache write failure still returns compositions", !writeFailure.compositions.empty());

		request.useCache = false;
		request.cacheRoot = testRoot / "unused-cache";
		GatedCompositionResult noCache = getOrCalculateGatedCompositions(request);
		passed &= expectResolution("no-cache calculates normally", noCache.resolution, CompositionResolution::CalculatedGates);
		passed &= expectTrue("no-cache does not create cache files", !filesystem::exists(request.cacheRoot));

		filesystem::remove_all(testRoot, cleanupError);
		return passed ? 0 : 1;
	}
	catch (const exception& error) {
		cerr << "Unexpected cache regression failure: " << error.what() << endl;
		filesystem::remove_all(testRoot, cleanupError);
		return 1;
	}
}
