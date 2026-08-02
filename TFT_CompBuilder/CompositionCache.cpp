#include "CompositionCache.h"
#include "json.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>

using json = nlohmann::json;
using namespace std;

namespace {
	constexpr int CACHE_SCHEMA_VERSION = 1;
	constexpr int GATE_ALGORITHM_VERSION = 1;
	constexpr int COMPOSITION_ALGORITHM_VERSION = 1;

	struct GateCacheRecord {
		GateTable table;
		array<bool, GateTable::SIZE> calculatedSizes{};
	};

	string buildProfile() {
#ifdef _DEBUG
		return "debug";
#else
		return "release";
#endif
	}

	string readFileBytes(const filesystem::path& path) {
		ifstream input(path, ios::binary);
		if (!input.is_open()) throw runtime_error("Could not open set data file for cache fingerprint: " + path.string());
		ostringstream contents;
		contents << input.rdbuf();
		if (input.bad()) throw runtime_error("Could not read set data file for cache fingerprint: " + path.string());
		return contents.str();
	}

	uint64_t fnv1a(const string& value, uint64_t seed) {
		uint64_t hash = seed;
		for (unsigned char character : value) {
			hash ^= character;
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	string stableFingerprint(const string& value) {
		uint64_t first = fnv1a(value, 14695981039346656037ULL);
		uint64_t second = fnv1a(value, 7809847782465536322ULL);
		ostringstream fingerprint;
		fingerprint << hex << setfill('0') << setw(16) << first << setw(16) << second;
		return fingerprint.str();
	}

	string setDataFingerprint(const GatedCompositionRequest& request) {
		string championData = readFileBytes(request.championInfoFile);
		string traitData = readFileBytes(request.traitInfoFile);
		return stableFingerprint(
			"champions:" + to_string(championData.size()) + ":" + championData +
			"\ntraits:" + to_string(traitData.size()) + ":" + traitData
		);
	}

	vector<string> canonicalEmblems(const vector<string>& emblems) {
		vector<string> canonical = emblems;
		sort(canonical.begin(), canonical.end());
		return canonical;
	}

	json makeGateKey(const GatedCompositionRequest& request, const string& dataFingerprint) {
		return {
			{ "set", request.setNumber },
			{ "set_data_fingerprint", dataFingerprint },
			{ "gate_type", gateTypeName(request.gateType) },
			{ "emblems", canonicalEmblems(request.emblemTraits) },
			{ "connected_champions_only", request.connectedChampsOnly },
			{ "gate_timeout_seconds", request.gateTimeoutSeconds },
			{ "build_profile", buildProfile() },
			{ "gate_algorithm_version", GATE_ALGORITHM_VERSION }
		};
	}

	json makeCompositionKey(const json& gateKey, int compositionSize) {
		return {
			{ "gate_profile", gateKey },
			{ "composition_size", compositionSize },
			{ "composition_algorithm_version", COMPOSITION_ALGORITHM_VERSION }
		};
	}

	filesystem::path cachePath(const filesystem::path& root, const string& kind, const json& key, const string& extension) {
		return root / kind / (stableFingerprint(key.dump()) + extension);
	}

	void replaceFile(const filesystem::path& temporaryPath, const filesystem::path& destinationPath) {
		error_code error;
		filesystem::rename(temporaryPath, destinationPath, error);
		if (!error) return;

		error.clear();
		filesystem::remove(destinationPath, error);
		error.clear();
		filesystem::rename(temporaryPath, destinationPath, error);
		if (error) {
			throw runtime_error("Could not replace cache file " + destinationPath.string() + ": " + error.message());
		}
	}

	array<int, GateTable::SIZE> gateRow(const GateTable& table, GateType gateType, int compositionSize) {
		return gateType == GateType::ActiveTraitTiers
			? table.activeTierGates[compositionSize - 1]
			: table.activeTraitGates[compositionSize - 1];
	}

	void setGateRow(GateTable& table, GateType gateType, int compositionSize, const array<int, GateTable::SIZE>& row) {
		if (gateType == GateType::ActiveTraitTiers) table.activeTierGates[compositionSize - 1] = row;
		else table.activeTraitGates[compositionSize - 1] = row;
	}

	string gateRowFingerprint(const GateTable& table, GateType gateType, int compositionSize) {
		ostringstream rowText;
		rowText << gateTypeName(gateType) << ':' << compositionSize;
		for (int gate : gateRow(table, gateType, compositionSize)) rowText << ':' << gate;
		return stableFingerprint(rowText.str());
	}

	optional<GateCacheRecord> readGateCache(const filesystem::path& path, const json& expectedKey) {
		if (!filesystem::exists(path)) return nullopt;
		try {
			ifstream input(path);
			if (!input.is_open()) return nullopt;
			json document = json::parse(input);
			if (document.at("schema_version").get<int>() != CACHE_SCHEMA_VERSION ||
				document.at("cache_kind").get<string>() != "gates" ||
				document.at("key") != expectedKey) {
				return nullopt;
			}

			GateCacheRecord record;
			for (const json& sizeValue : document.at("calculated_sizes")) {
				int size = sizeValue.get<int>();
				if (size < 1 || size > GateTable::SIZE) throw runtime_error("invalid calculated size");
				const json& rowJson = document.at("gate_rows").at(to_string(size));
				if (!rowJson.is_array() || rowJson.size() != GateTable::SIZE) throw runtime_error("invalid gate row");
				array<int, GateTable::SIZE> row{};
				for (int i = 0; i < GateTable::SIZE; ++i) row[i] = rowJson.at(i).get<int>();
				setGateRow(record.table, expectedKey.at("gate_type").get<string>() == "tiers" ? GateType::ActiveTraitTiers : GateType::ActiveTraits, size, row);
				record.calculatedSizes[size - 1] = true;
			}
			return record;
		}
		catch (const exception& error) {
			cerr << "Ignoring invalid gate cache " << path.string() << ": " << error.what() << endl;
			return nullopt;
		}
	}

	void writeGateCache(const filesystem::path& path, const json& key, const GateCacheRecord& record, GateType gateType) {
		filesystem::create_directories(path.parent_path());
		json calculatedSizes = json::array();
		json rows = json::object();
		for (int size = 1; size <= GateTable::SIZE; ++size) {
			if (!record.calculatedSizes[size - 1]) continue;
			calculatedSizes.push_back(size);
			rows[to_string(size)] = gateRow(record.table, gateType, size);
		}

		json document = {
			{ "schema_version", CACHE_SCHEMA_VERSION },
			{ "cache_kind", "gates" },
			{ "key", key },
			{ "calculated_sizes", calculatedSizes },
			{ "gate_rows", rows }
		};
		filesystem::path temporaryPath = path;
		temporaryPath += ".tmp";
		ofstream output(temporaryPath, ios::trunc);
		if (!output.is_open()) throw runtime_error("Could not open gate cache for writing: " + temporaryPath.string());
		output << document.dump(2) << '\n';
		output.close();
		if (!output) throw runtime_error("Could not write gate cache: " + temporaryPath.string());
		replaceFile(temporaryPath, path);
	}

	optional<vector<TeamComposition>> readCompositionCache(
		const filesystem::path& path,
		const json& expectedKey,
		const string& expectedGateFingerprint,
		const TeamComposition& seedComposition,
		int expectedCompositionSize
	) {
		if (!filesystem::exists(path)) return nullopt;
		try {
			ifstream input(path);
			if (!input.is_open()) return nullopt;
			string line;
			if (!getline(input, line)) throw runtime_error("missing header");
			json header = json::parse(line);
			if (header.at("schema_version").get<int>() != CACHE_SCHEMA_VERSION ||
				header.at("cache_kind").get<string>() != "compositions" ||
				header.at("key") != expectedKey ||
				header.at("gate_row_fingerprint").get<string>() != expectedGateFingerprint) {
				return nullopt;
			}

			size_t expectedCount = header.at("composition_count").get<size_t>();
			vector<TeamComposition> compositions;
			compositions.reserve(expectedCount);
			while (getline(input, line)) {
				if (line.empty()) continue;
				vector<string> labels = json::parse(line).get<vector<string>>();
				TeamComposition composition(seedComposition);
				for (const string& label : labels) {
					if (!composition.addChamp(label)) throw runtime_error("duplicate champion variant");
				}
				if (composition.size() != expectedCompositionSize) throw runtime_error("composition has the wrong board width");
				compositions.push_back(move(composition));
			}
			if (compositions.size() != expectedCount) throw runtime_error("composition count does not match header");
			return compositions;
		}
		catch (const exception& error) {
			cerr << "Ignoring invalid composition cache " << path.string() << ": " << error.what() << endl;
			return nullopt;
		}
	}

	void writeCompositionCache(
		const filesystem::path& path,
		const json& key,
		const string& gateFingerprint,
		const vector<TeamComposition>& compositions
	) {
		filesystem::create_directories(path.parent_path());
		filesystem::path temporaryPath = path;
		temporaryPath += ".tmp";
		ofstream output(temporaryPath, ios::trunc);
		if (!output.is_open()) throw runtime_error("Could not open composition cache for writing: " + temporaryPath.string());
		json header = {
			{ "schema_version", CACHE_SCHEMA_VERSION },
			{ "cache_kind", "compositions" },
			{ "key", key },
			{ "gate_row_fingerprint", gateFingerprint },
			{ "composition_count", compositions.size() }
		};
		output << header.dump() << '\n';
		for (const TeamComposition& composition : compositions) {
			output << json(composition.getChampionLabels()).dump() << '\n';
		}
		output.close();
		if (!output) throw runtime_error("Could not write composition cache: " + temporaryPath.string());
		replaceFile(temporaryPath, path);
	}

	void validateRequest(const GatedCompositionRequest& request) {
		if (request.setNumber.empty()) throw runtime_error("A set number is required for cached composition generation.");
		if (request.compositionSize < 1 || request.compositionSize > GateTable::SIZE) {
			throw runtime_error("Composition size must be between 1 and " + to_string(GateTable::SIZE) + ".");
		}
		if (request.gateTimeoutSeconds < 1) throw runtime_error("Gate timeout must be at least 1 second.");
	}
}

const char* gateTypeName(GateType gateType) {
	return gateType == GateType::ActiveTraitTiers ? "tiers" : "traits";
}

const char* compositionResolutionName(CompositionResolution resolution) {
	switch (resolution) {
	case CompositionResolution::CompositionCacheHit: return "composition cache";
	case CompositionResolution::GateCacheHit: return "gate cache";
	default: return "calculated gates";
	}
}

GatedCompositionResult getOrCalculateGatedCompositions(const GatedCompositionRequest& request) {
	validateRequest(request);
	string dataFingerprint = setDataFingerprint(request);
	json gateKey = makeGateKey(request, dataFingerprint);
	json compositionKey = makeCompositionKey(gateKey, request.compositionSize);
	filesystem::path gatePath = cachePath(request.cacheRoot, "gates", gateKey, ".json");
	filesystem::path compositionPath = cachePath(request.cacheRoot, "compositions", compositionKey, ".jsonl");

	TeamComposition seedComposition;
	for (const string& emblem : request.emblemTraits) seedComposition.incrementTrait(emblem);

	optional<GateCacheRecord> cachedGates;
	if (request.useCache && !request.refreshCache) cachedGates = readGateCache(gatePath, gateKey);
	if (cachedGates.has_value() && cachedGates->calculatedSizes[request.compositionSize - 1]) {
		TeamComposition::setGateTable(cachedGates->table);
		string rowFingerprint = gateRowFingerprint(cachedGates->table, request.gateType, request.compositionSize);
		if (optional<vector<TeamComposition>> cachedCompositions = readCompositionCache(
			compositionPath,
			compositionKey,
			rowFingerprint,
			seedComposition,
			request.compositionSize
		)) {
			return { move(*cachedCompositions), CompositionResolution::CompositionCacheHit };
		}

		int settings[3] = { 0, request.gateType == GateType::ActiveTraitTiers ? 1 : 0, request.connectedChampsOnly ? 1 : 0 };
		vector<TeamComposition> compositions = TeamComposition::generateComps(request.compositionSize, settings, seedComposition);
		if (request.useCache) writeCompositionCache(compositionPath, compositionKey, rowFingerprint, compositions);
		return { move(compositions), CompositionResolution::GateCacheHit };
	}

	GateCacheRecord updatedGates = cachedGates.value_or(GateCacheRecord{});
	TeamComposition::setGateTable(updatedGates.table);
	GateTable calculatedTable = TeamComposition::calculateGateTable(
		true,
		request.gateTimeoutSeconds,
		request.compositionSize,
		request.gateType == GateType::ActiveTraitTiers ? 1 : 0,
		seedComposition,
		request.connectedChampsOnly
	);
	updatedGates.table = calculatedTable;
	updatedGates.calculatedSizes[request.compositionSize - 1] = true;
	if (request.useCache) writeGateCache(gatePath, gateKey, updatedGates, request.gateType);

	int settings[3] = { 0, request.gateType == GateType::ActiveTraitTiers ? 1 : 0, request.connectedChampsOnly ? 1 : 0 };
	vector<TeamComposition> compositions = TeamComposition::generateComps(request.compositionSize, settings, seedComposition);
	string rowFingerprint = gateRowFingerprint(calculatedTable, request.gateType, request.compositionSize);
	if (request.useCache) writeCompositionCache(compositionPath, compositionKey, rowFingerprint, compositions);
	return { move(compositions), CompositionResolution::CalculatedGates };
}
