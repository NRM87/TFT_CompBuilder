#pragma once

#include "TeamComposition.h"

#include <filesystem>
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

const char* gateTypeName(GateType gateType);
const char* compositionResolutionName(CompositionResolution resolution);
GatedCompositionResult getOrCalculateGatedCompositions(const GatedCompositionRequest& request);
