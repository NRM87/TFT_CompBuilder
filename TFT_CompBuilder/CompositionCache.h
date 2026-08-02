#pragma once

#include "TeamComposition.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

enum class GateType {
	ActiveTraits = 0,
	ActiveTraitTiers = 1
};

enum class CompositionResolution {
	CompositionCacheHit,
	GateCacheHit,
	CalculatedGates
};

struct GatedCompositionRequest {
	std::string setNumber;
	std::filesystem::path championInfoFile;
	std::filesystem::path traitInfoFile;
	int compositionSize = 9;
	GateType gateType = GateType::ActiveTraits;
	std::vector<std::string> emblemTraits;
	bool connectedChampsOnly = false;
	int gateTimeoutSeconds = 10;
	std::filesystem::path cacheRoot = "Cache";
	bool useCache = true;
	bool refreshCache = false;
};

struct GatedCompositionResult {
	std::vector<TeamComposition> compositions;
	CompositionResolution resolution = CompositionResolution::CalculatedGates;
};

struct CompositionCacheInventory {
	std::uintmax_t totalBytes = 0;
	std::uintmax_t objectBytes = 0;
	std::uintmax_t orphanedBytes = 0;
	std::uintmax_t legacyBytes = 0;
	std::uint64_t totalFiles = 0;
	std::uint64_t gateManifests = 0;
	std::uint64_t compositionManifests = 0;
	std::uint64_t gateObjects = 0;
	std::uint64_t compositionObjects = 0;
	std::uint64_t orphanedObjects = 0;
	std::uint64_t invalidManifests = 0;
	std::uint64_t temporaryFiles = 0;
	std::uint64_t legacyFiles = 0;
	std::uint64_t cachedCompositions = 0;
};

struct CompositionCachePruneResult {
	CompositionCacheInventory before;
	CompositionCacheInventory after;
	std::uint64_t removedFiles = 0;
	std::uintmax_t removedBytes = 0;
};

const char* gateTypeName(GateType gateType);
const char* compositionResolutionName(CompositionResolution resolution);
GatedCompositionResult getOrCalculateGatedCompositions(const GatedCompositionRequest& request);
CompositionCacheInventory inspectCompositionCache(const std::filesystem::path& cacheRoot = "Cache");
CompositionCachePruneResult pruneCompositionCache(
	const std::filesystem::path& cacheRoot = "Cache",
	std::optional<std::uintmax_t> maximumBytes = std::nullopt
);
