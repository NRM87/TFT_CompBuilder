#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include "CompositionCache.h"
#include "json.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

using json = nlohmann::json;
using namespace std;

namespace {
	constexpr int CACHE_SCHEMA_VERSION = 2;
	constexpr size_t HASH_BUFFER_SIZE = 64 * 1024;

	struct GateCacheRecord {
		GateTable table;
		array<bool, GateTable::SIZE> calculatedSizes{};
	};

	struct CachedGateRecord {
		GateCacheRecord record;
		string objectHash;
	};

	struct CacheKeys {
		json gateKey;
		json compositionKey;
		filesystem::path gateManifestPath;
		filesystem::path compositionManifestPath;
	};

	struct ManifestEntry {
		filesystem::path path;
		vector<filesystem::path> referencedObjects;
		filesystem::file_time_type lastWriteTime{};
	};

	struct CacheScan {
		CompositionCacheInventory inventory;
		vector<ManifestEntry> manifests;
		vector<filesystem::path> invalidManifests;
		vector<filesystem::path> temporaryFiles;
		vector<filesystem::path> legacyFiles;
		vector<filesystem::path> objectFiles;
		unordered_set<string> reachableObjects;
	};

	class Sha256Hasher {
	public:
		Sha256Hasher() {
			check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0), "open SHA-256 provider");
			DWORD objectSize = 0;
			DWORD bytesRead = 0;
			check(BCryptGetProperty(
				algorithm,
				BCRYPT_OBJECT_LENGTH,
				reinterpret_cast<PUCHAR>(&objectSize),
				sizeof(objectSize),
				&bytesRead,
				0
			), "read SHA-256 object size");
			hashObject.resize(objectSize);
			check(BCryptCreateHash(algorithm, &hash, hashObject.data(), objectSize, nullptr, 0, 0), "create SHA-256 hash");
		}

		Sha256Hasher(const Sha256Hasher&) = delete;
		Sha256Hasher& operator=(const Sha256Hasher&) = delete;

		~Sha256Hasher() {
			if (hash != nullptr) BCryptDestroyHash(hash);
			if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
		}

		void update(const void* data, size_t size) {
			const unsigned char* bytes = static_cast<const unsigned char*>(data);
			while (size > 0) {
				ULONG chunkSize = static_cast<ULONG>(min<size_t>(size, numeric_limits<ULONG>::max()));
				check(BCryptHashData(hash, const_cast<PUCHAR>(bytes), chunkSize, 0), "update SHA-256 hash");
				bytes += chunkSize;
				size -= chunkSize;
			}
		}

		string finish() {
			array<unsigned char, 32> digest{};
			check(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0), "finish SHA-256 hash");
			BCryptDestroyHash(hash);
			hash = nullptr;

			ostringstream encoded;
			encoded << hex << setfill('0');
			for (unsigned char byte : digest) encoded << setw(2) << static_cast<int>(byte);
			return encoded.str();
		}

	private:
		static void check(NTSTATUS status, const string& operation) {
			if (status < 0) throw runtime_error("Could not " + operation + ".");
		}

		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		vector<unsigned char> hashObject;
	};

	string sha256(const string& value) {
		Sha256Hasher hasher;
		hasher.update(value.data(), value.size());
		return hasher.finish();
	}

	string sha256File(const filesystem::path& path) {
		ifstream input(path, ios::binary);
		if (!input.is_open()) throw runtime_error("Could not open file for SHA-256: " + path.string());
		Sha256Hasher hasher;
		array<char, HASH_BUFFER_SIZE> buffer{};
		while (input) {
			input.read(buffer.data(), static_cast<streamsize>(buffer.size()));
			streamsize bytesRead = input.gcount();
			if (bytesRead > 0) hasher.update(buffer.data(), static_cast<size_t>(bytesRead));
		}
		if (!input.eof()) throw runtime_error("Could not read file for SHA-256: " + path.string());
		return hasher.finish();
	}

	string currentExecutableFingerprint() {
		static const string fingerprint = [] {
			vector<wchar_t> pathBuffer(32768);
			DWORD length = GetModuleFileNameW(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
			if (length == 0 || length >= pathBuffer.size()) {
				throw runtime_error("Could not locate the running executable for cache invalidation.");
			}
			return sha256File(filesystem::path(pathBuffer.data(), pathBuffer.data() + length));
		}();
		return fingerprint;
	}

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

	string setDataFingerprint(const GatedCompositionRequest& request) {
		string championData = readFileBytes(request.championInfoFile);
		string traitData = readFileBytes(request.traitInfoFile);
		return sha256(
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
			{ "algorithm_fingerprint", currentExecutableFingerprint() }
		};
	}

	json makeCompositionKey(const json& gateKey, int compositionSize) {
		return {
			{ "gate_profile", gateKey },
			{ "composition_size", compositionSize }
		};
	}

	filesystem::path v2Root(const filesystem::path& cacheRoot) {
		return cacheRoot / "v2";
	}

	filesystem::path gateObjectPath(const filesystem::path& cacheRoot, const string& hash) {
		return v2Root(cacheRoot) / "objects" / "gates" / (hash + ".json");
	}

	filesystem::path compositionObjectPath(const filesystem::path& cacheRoot, const string& hash) {
		return v2Root(cacheRoot) / "objects" / "compositions" / (hash + ".jsonl");
	}

	CacheKeys makeCacheKeys(const GatedCompositionRequest& request) {
		string dataFingerprint = setDataFingerprint(request);
		json gateKey = makeGateKey(request, dataFingerprint);
		json compositionKey = makeCompositionKey(gateKey, request.compositionSize);
		return {
			gateKey,
			compositionKey,
			v2Root(request.cacheRoot) / "manifests" / "gates" / (sha256(gateKey.dump()) + ".json"),
			v2Root(request.cacheRoot) / "manifests" / "compositions" / (sha256(compositionKey.dump()) + ".json")
		};
	}

	string normalizedPathKey(const filesystem::path& path) {
		error_code error;
		filesystem::path absolutePath = filesystem::absolute(path, error);
		string key = (error ? path : absolutePath).lexically_normal().generic_string();
		transform(key.begin(), key.end(), key.begin(), [](unsigned char character) { return static_cast<char>(tolower(character)); });
		return key;
	}

	bool isWithin(const filesystem::path& path, const filesystem::path& directory) {
		string pathKey = normalizedPathKey(path);
		string directoryKey = normalizedPathKey(directory);
		if (!directoryKey.ends_with('/')) directoryKey += '/';
		return pathKey.starts_with(directoryKey);
	}

	bool isSha256(const string& value) {
		if (value.size() != 64) return false;
		return all_of(value.begin(), value.end(), [](unsigned char character) { return isxdigit(character) != 0; });
	}

	class CacheWriteLock {
	public:
		explicit CacheWriteLock(const filesystem::path& cacheRoot) {
			string hash = sha256(normalizedPathKey(cacheRoot));
			wstring name = L"Local\\TFTCompBuilderCache-" + wstring(hash.begin(), hash.end());
			handle = CreateMutexW(nullptr, FALSE, name.c_str());
			if (handle == nullptr) throw runtime_error("Could not create the cache write mutex.");
			DWORD waitResult = WaitForSingleObject(handle, INFINITE);
			if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED) {
				CloseHandle(handle);
				handle = nullptr;
				throw runtime_error("Could not acquire the cache write mutex.");
			}
		}

		CacheWriteLock(const CacheWriteLock&) = delete;
		CacheWriteLock& operator=(const CacheWriteLock&) = delete;

		~CacheWriteLock() {
			if (handle != nullptr) {
				ReleaseMutex(handle);
				CloseHandle(handle);
			}
		}

	private:
		HANDLE handle = nullptr;
	};

	atomic<uint64_t> temporaryFileCounter = 0;

	filesystem::path makeTemporaryPath(const filesystem::path& cacheRoot, const string& extension) {
		filesystem::path stagingDirectory = v2Root(cacheRoot) / "staging";
		filesystem::create_directories(stagingDirectory);
		uint64_t counter = temporaryFileCounter.fetch_add(1, memory_order_relaxed);
		return stagingDirectory /
			("cache.tmp." + to_string(GetCurrentProcessId()) + "." + to_string(counter) + extension);
	}

	void removeNoThrow(const filesystem::path& path) {
		error_code error;
		filesystem::remove(path, error);
	}

	void writeTextFile(const filesystem::path& path, const string& contents) {
		ofstream output(path, ios::binary | ios::trunc);
		if (!output.is_open()) throw runtime_error("Could not open cache file for writing: " + path.string());
		output.write(contents.data(), static_cast<streamsize>(contents.size()));
		output.close();
		if (!output) throw runtime_error("Could not write cache file: " + path.string());
	}

	void replaceManifest(const filesystem::path& temporaryPath, const filesystem::path& destinationPath) {
		filesystem::create_directories(destinationPath.parent_path());
		BOOL succeeded = FALSE;
		if (filesystem::exists(destinationPath)) {
			succeeded = ReplaceFileW(
				destinationPath.c_str(),
				temporaryPath.c_str(),
				nullptr,
				REPLACEFILE_WRITE_THROUGH,
				nullptr,
				nullptr
			);
		}
		else {
			succeeded = MoveFileExW(temporaryPath.c_str(), destinationPath.c_str(), MOVEFILE_WRITE_THROUGH);
		}
		if (!succeeded) {
			DWORD errorCode = GetLastError();
			removeNoThrow(temporaryPath);
			throw runtime_error(
				"Could not atomically publish cache manifest " + destinationPath.string() +
				" (Windows error " + to_string(errorCode) + ")."
			);
		}
	}

	void writeManifest(const filesystem::path& cacheRoot, const filesystem::path& path, const json& document) {
		filesystem::path temporaryPath = makeTemporaryPath(cacheRoot, ".json");
		try {
			writeTextFile(temporaryPath, document.dump(2) + "\n");
			replaceManifest(temporaryPath, path);
		}
		catch (...) {
			removeNoThrow(temporaryPath);
			throw;
		}
	}

	string publishStagedObject(
		const filesystem::path& cacheRoot,
		const filesystem::path& temporaryPath,
		const filesystem::path& objectDirectory,
		const string& extension
	) {
		string objectHash = sha256File(temporaryPath);
		filesystem::create_directories(objectDirectory);
		filesystem::path destinationPath = objectDirectory / (objectHash + extension);
		if (filesystem::exists(destinationPath)) {
			if (sha256File(destinationPath) != objectHash) {
				removeNoThrow(destinationPath);
			}
			else {
				removeNoThrow(temporaryPath);
				return objectHash;
			}
		}

		error_code error;
		filesystem::rename(temporaryPath, destinationPath, error);
		if (error) {
			removeNoThrow(temporaryPath);
			throw runtime_error("Could not publish immutable cache object " + destinationPath.string() + ": " + error.message());
		}
		return objectHash;
	}

	optional<json> readManifest(const filesystem::path& path, const string& kind, const json& expectedKey, bool warnOnInvalid = true) {
		error_code existenceError;
		bool exists = filesystem::exists(path, existenceError);
		if (existenceError || !exists) return nullopt;
		try {
			ifstream input(path);
			if (!input.is_open()) return nullopt;
			json document = json::parse(input);
			if (document.at("schema_version").get<int>() != CACHE_SCHEMA_VERSION ||
				document.at("cache_kind").get<string>() != kind ||
				document.at("key") != expectedKey) {
				throw runtime_error("manifest metadata does not match its request");
			}
			return document;
		}
		catch (const exception& error) {
			if (warnOnInvalid) cerr << "Ignoring invalid cache manifest " << path.string() << ": " << error.what() << endl;
			return nullopt;
		}
	}

	void touchNoThrow(const filesystem::path& path) {
		error_code error;
		filesystem::last_write_time(path, filesystem::file_time_type::clock::now(), error);
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
		return sha256(rowText.str());
	}

	json serializeGateObject(const GateCacheRecord& record, GateType gateType) {
		json calculatedSizes = json::array();
		json rows = json::object();
		for (int size = 1; size <= GateTable::SIZE; ++size) {
			if (!record.calculatedSizes[size - 1]) continue;
			calculatedSizes.push_back(size);
			rows[to_string(size)] = gateRow(record.table, gateType, size);
		}
		return {
			{ "schema_version", CACHE_SCHEMA_VERSION },
			{ "cache_kind", "gate_object" },
			{ "gate_type", gateTypeName(gateType) },
			{ "calculated_sizes", calculatedSizes },
			{ "gate_rows", rows }
		};
	}

	GateCacheRecord readGateObject(const filesystem::path& cacheRoot, const string& objectHash, GateType gateType) {
		if (!isSha256(objectHash)) throw runtime_error("invalid gate object hash");
		filesystem::path path = gateObjectPath(cacheRoot, objectHash);
		if (sha256File(path) != objectHash) throw runtime_error("gate object checksum mismatch");
		ifstream input(path);
		json document = json::parse(input);
		if (document.at("schema_version").get<int>() != CACHE_SCHEMA_VERSION ||
			document.at("cache_kind").get<string>() != "gate_object" ||
			document.at("gate_type").get<string>() != gateTypeName(gateType)) {
			throw runtime_error("gate object metadata mismatch");
		}

		GateCacheRecord record;
		for (const json& sizeValue : document.at("calculated_sizes")) {
			int size = sizeValue.get<int>();
			if (size < 1 || size > GateTable::SIZE) throw runtime_error("invalid calculated gate size");
			const json& rowJson = document.at("gate_rows").at(to_string(size));
			if (!rowJson.is_array() || rowJson.size() != GateTable::SIZE) throw runtime_error("invalid gate row");
			array<int, GateTable::SIZE> row{};
			for (int i = 0; i < GateTable::SIZE; ++i) {
				row[i] = rowJson.at(i).get<int>();
				if (row[i] < 0) throw runtime_error("gate values cannot be negative");
			}
			setGateRow(record.table, gateType, size, row);
			record.calculatedSizes[size - 1] = true;
		}
		return record;
	}

	optional<CachedGateRecord> readGateCache(const GatedCompositionRequest& request, const CacheKeys& keys) {
		optional<json> manifest = readManifest(keys.gateManifestPath, "gate_manifest", keys.gateKey);
		if (!manifest.has_value()) return nullopt;
		try {
			string objectHash = manifest->at("gate_object").get<string>();
			GateCacheRecord record = readGateObject(request.cacheRoot, objectHash, request.gateType);
			touchNoThrow(keys.gateManifestPath);
			return CachedGateRecord{ move(record), move(objectHash) };
		}
		catch (const exception& error) {
			cerr << "Ignoring invalid gate cache " << keys.gateManifestPath.string() << ": " << error.what() << endl;
			return nullopt;
		}
	}

	string writeGateCache(
		const GatedCompositionRequest& request,
		const CacheKeys& keys,
		GateCacheRecord record
	) {
		CacheWriteLock lock(request.cacheRoot);
		if (optional<json> currentManifest = readManifest(keys.gateManifestPath, "gate_manifest", keys.gateKey, false)) {
			try {
				GateCacheRecord current = readGateObject(
					request.cacheRoot,
					currentManifest->at("gate_object").get<string>(),
					request.gateType
				);
				for (int size = 1; size <= GateTable::SIZE; ++size) {
					if (record.calculatedSizes[size - 1] || !current.calculatedSizes[size - 1]) continue;
					setGateRow(record.table, request.gateType, size, gateRow(current.table, request.gateType, size));
					record.calculatedSizes[size - 1] = true;
				}
			}
			catch (const exception&) {
				// A corrupt current object is replaced by the new valid record.
			}
		}

		filesystem::path temporaryPath = makeTemporaryPath(request.cacheRoot, ".json");
		string objectHash;
		try {
			writeTextFile(temporaryPath, serializeGateObject(record, request.gateType).dump(2) + "\n");
			objectHash = publishStagedObject(
				request.cacheRoot,
				temporaryPath,
				v2Root(request.cacheRoot) / "objects" / "gates",
				".json"
			);
		}
		catch (...) {
			removeNoThrow(temporaryPath);
			throw;
		}

		json manifest = {
			{ "schema_version", CACHE_SCHEMA_VERSION },
			{ "cache_kind", "gate_manifest" },
			{ "key", keys.gateKey },
			{ "gate_object", objectHash }
		};
		writeManifest(request.cacheRoot, keys.gateManifestPath, manifest);
		return objectHash;
	}

	optional<vector<TeamComposition>> readCompositionCache(
		const GatedCompositionRequest& request,
		const CacheKeys& keys,
		const TeamComposition& seedComposition
	) {
		optional<json> manifest = readManifest(keys.compositionManifestPath, "composition_manifest", keys.compositionKey);
		if (!manifest.has_value()) return nullopt;
		try {
			string objectHash = manifest->at("composition_object").get<string>();
			if (!isSha256(objectHash)) throw runtime_error("invalid composition object hash");
			filesystem::path objectPath = compositionObjectPath(request.cacheRoot, objectHash);
			if (sha256File(objectPath) != objectHash) throw runtime_error("composition object checksum mismatch");

			ifstream input(objectPath);
			if (!input.is_open()) throw runtime_error("could not open composition object");
			string line;
			if (!getline(input, line)) throw runtime_error("composition object is missing its header");
			json header = json::parse(line);
			if (header.at("schema_version").get<int>() != CACHE_SCHEMA_VERSION ||
				header.at("cache_kind").get<string>() != "composition_object" ||
				header.at("encoding").get<string>() != "label_dictionary_v1" ||
				header.at("composition_size").get<int>() != request.compositionSize) {
				throw runtime_error("composition object metadata mismatch");
			}

			vector<string> dictionary = header.at("labels").get<vector<string>>();
			unordered_set<string> uniqueLabels(dictionary.begin(), dictionary.end());
			if (uniqueLabels.size() != dictionary.size()) throw runtime_error("composition label dictionary contains duplicates");
			size_t expectedCount = header.at("composition_count").get<size_t>();
			if (manifest->at("composition_count").get<size_t>() != expectedCount) {
				throw runtime_error("composition manifest count mismatch");
			}

			vector<TeamComposition> compositions;
			compositions.reserve(expectedCount);
			while (getline(input, line)) {
				if (line.empty()) continue;
				vector<int> labelIds = json::parse(line).get<vector<int>>();
				TeamComposition composition(seedComposition);
				for (int labelId : labelIds) {
					if (labelId < 0 || static_cast<size_t>(labelId) >= dictionary.size()) {
						throw runtime_error("composition label ID is outside the dictionary");
					}
					if (!composition.addChamp(dictionary[labelId])) throw runtime_error("composition contains a duplicate champion variant");
				}
				if (composition.size() != request.compositionSize) throw runtime_error("composition has the wrong board width");
				compositions.push_back(move(composition));
			}
			if (compositions.size() != expectedCount) throw runtime_error("composition count does not match its object header");
			touchNoThrow(keys.compositionManifestPath);
			return compositions;
		}
		catch (const exception& error) {
			cerr << "Ignoring invalid composition cache " << keys.compositionManifestPath.string() << ": " << error.what() << endl;
			return nullopt;
		}
	}

	string writeCompositionObject(
		const GatedCompositionRequest& request,
		const vector<TeamComposition>& compositions
	) {
		set<string> uniqueLabels;
		for (const TeamComposition& composition : compositions) {
			for (const string& label : composition.getChampionLabels()) uniqueLabels.insert(label);
		}
		vector<string> dictionary(uniqueLabels.begin(), uniqueLabels.end());
		unordered_map<string, int> labelIds;
		for (int i = 0; i < static_cast<int>(dictionary.size()); ++i) labelIds.emplace(dictionary[i], i);

		filesystem::path temporaryPath = makeTemporaryPath(request.cacheRoot, ".jsonl");
		try {
			ofstream output(temporaryPath, ios::binary | ios::trunc);
			if (!output.is_open()) throw runtime_error("Could not open composition object for writing: " + temporaryPath.string());
			json header = {
				{ "schema_version", CACHE_SCHEMA_VERSION },
				{ "cache_kind", "composition_object" },
				{ "encoding", "label_dictionary_v1" },
				{ "composition_size", request.compositionSize },
				{ "composition_count", compositions.size() },
				{ "labels", dictionary }
			};
			output << header.dump() << '\n';
			for (const TeamComposition& composition : compositions) {
				vector<int> encoded;
				for (const string& label : composition.getChampionLabels()) encoded.push_back(labelIds.at(label));
				output << json(encoded).dump() << '\n';
			}
			output.close();
			if (!output) throw runtime_error("Could not write composition object: " + temporaryPath.string());
			return publishStagedObject(
				request.cacheRoot,
				temporaryPath,
				v2Root(request.cacheRoot) / "objects" / "compositions",
				".jsonl"
			);
		}
		catch (...) {
			removeNoThrow(temporaryPath);
			throw;
		}
	}

	void writeCompositionCache(
		const GatedCompositionRequest& request,
		const CacheKeys& keys,
		const string& gateFingerprint,
		const optional<string>& gateObjectHash,
		const vector<TeamComposition>& compositions
	) {
		CacheWriteLock lock(request.cacheRoot);
		string compositionObjectHash = writeCompositionObject(request, compositions);
		json manifest = {
			{ "schema_version", CACHE_SCHEMA_VERSION },
			{ "cache_kind", "composition_manifest" },
			{ "key", keys.compositionKey },
			{ "gate_row_fingerprint", gateFingerprint },
			{ "composition_object", compositionObjectHash },
			{ "composition_count", compositions.size() }
		};
		if (gateObjectHash.has_value()) manifest["gate_object"] = *gateObjectHash;
		writeManifest(request.cacheRoot, keys.compositionManifestPath, manifest);
	}

	void invalidateCompositionManifest(const GatedCompositionRequest& request, const CacheKeys& keys) {
		CacheWriteLock lock(request.cacheRoot);
		error_code error;
		filesystem::remove(keys.compositionManifestPath, error);
		if (error) throw runtime_error("Could not invalidate composition manifest: " + error.message());
	}

	void warnCacheFailure(const string& operation, const exception& error) {
		cerr << "Cache warning: could not " << operation << ": " << error.what() << ". Continuing without that cache update." << endl;
	}

	void validateRequest(const GatedCompositionRequest& request) {
		if (request.setNumber.empty()) throw runtime_error("A set number is required for cached composition generation.");
		if (request.compositionSize < 1 || request.compositionSize > GateTable::SIZE) {
			throw runtime_error("Composition size must be between 1 and " + to_string(GateTable::SIZE) + ".");
		}
		if (request.gateTimeoutSeconds < 1) throw runtime_error("Gate timeout must be at least 1 second.");
	}

	TeamComposition makeSeedComposition(const vector<string>& emblems) {
		TeamComposition seed;
		for (const string& emblem : emblems) seed.incrementTrait(emblem);
		return seed;
	}

	GatedCompositionResult calculateWithoutCache(
		const GatedCompositionRequest& request,
		const TeamComposition& seedComposition
	) {
		TeamComposition::setGateTable(GateTable{});
		TeamComposition::calculateGateTable(
			true,
			request.gateTimeoutSeconds,
			request.compositionSize,
			request.gateType == GateType::ActiveTraitTiers ? 1 : 0,
			seedComposition,
			request.connectedChampsOnly
		);
		int settings[3] = { 0, request.gateType == GateType::ActiveTraitTiers ? 1 : 0, request.connectedChampsOnly ? 1 : 0 };
		return {
			TeamComposition::generateComps(request.compositionSize, settings, seedComposition),
			CompositionResolution::CalculatedGates
		};
	}

	uintmax_t safeFileSize(const filesystem::path& path) {
		error_code error;
		uintmax_t size = filesystem::file_size(path, error);
		return error ? 0 : size;
	}

	optional<json> readAnyManifest(const filesystem::path& path) {
		try {
			ifstream input(path);
			if (!input.is_open()) return nullopt;
			json document = json::parse(input);
			if (document.at("schema_version").get<int>() != CACHE_SCHEMA_VERSION) return nullopt;
			return document;
		}
		catch (const exception&) {
			return nullopt;
		}
	}

	CacheScan scanCache(const filesystem::path& cacheRoot) {
		CacheScan scan;
		error_code error;
		if (!filesystem::exists(cacheRoot, error) || error) return scan;

		filesystem::path gateManifestDirectory = v2Root(cacheRoot) / "manifests" / "gates";
		filesystem::path compositionManifestDirectory = v2Root(cacheRoot) / "manifests" / "compositions";
		filesystem::path gateObjectDirectory = v2Root(cacheRoot) / "objects" / "gates";
		filesystem::path compositionObjectDirectory = v2Root(cacheRoot) / "objects" / "compositions";
		filesystem::path stagingDirectory = v2Root(cacheRoot) / "staging";
		filesystem::path legacyGateDirectory = cacheRoot / "gates";
		filesystem::path legacyCompositionDirectory = cacheRoot / "compositions";

		filesystem::recursive_directory_iterator iterator(cacheRoot, filesystem::directory_options::skip_permission_denied, error);
		filesystem::recursive_directory_iterator end;
		for (; iterator != end; iterator.increment(error)) {
			if (error) {
				error.clear();
				continue;
			}
			if (!iterator->is_regular_file(error) || error) {
				error.clear();
				continue;
			}

			filesystem::path path = iterator->path();
			uintmax_t size = safeFileSize(path);
			++scan.inventory.totalFiles;
			scan.inventory.totalBytes += size;

			if (isWithin(path, legacyGateDirectory) || isWithin(path, legacyCompositionDirectory)) {
				++scan.inventory.legacyFiles;
				scan.inventory.legacyBytes += size;
				scan.legacyFiles.push_back(path);
				continue;
			}
			if (isWithin(path, stagingDirectory) || path.filename().string().find(".tmp.") != string::npos) {
				++scan.inventory.temporaryFiles;
				scan.temporaryFiles.push_back(path);
				continue;
			}

			bool gateManifest = path.parent_path() == gateManifestDirectory;
			bool compositionManifest = path.parent_path() == compositionManifestDirectory;
			if (gateManifest || compositionManifest) {
				optional<json> document = readAnyManifest(path);
				if (!document.has_value()) {
					++scan.inventory.invalidManifests;
					scan.invalidManifests.push_back(path);
					continue;
				}
				try {
					ManifestEntry entry;
					entry.path = path;
					entry.lastWriteTime = filesystem::last_write_time(path);
					if (!document->at("key").is_object()) throw runtime_error("bad key");
					string kind = document->at("cache_kind").get<string>();
					if (gateManifest && kind == "gate_manifest") {
						string gateHash = document->at("gate_object").get<string>();
						if (!isSha256(gateHash)) throw runtime_error("bad hash");
						filesystem::path gatePath = gateObjectPath(cacheRoot, gateHash);
						error_code objectError;
						if (!filesystem::is_regular_file(gatePath, objectError) || objectError) {
							throw runtime_error("missing gate object");
						}
						entry.referencedObjects.push_back(move(gatePath));
						++scan.inventory.gateManifests;
					}
					else if (compositionManifest && kind == "composition_manifest") {
						string compositionHash = document->at("composition_object").get<string>();
						if (!isSha256(compositionHash)) throw runtime_error("bad hash");
						filesystem::path compositionPath = compositionObjectPath(cacheRoot, compositionHash);
						error_code objectError;
						if (!filesystem::is_regular_file(compositionPath, objectError) || objectError) {
							throw runtime_error("missing composition object");
						}
						entry.referencedObjects.push_back(move(compositionPath));
						if (document->contains("gate_object")) {
							string gateHash = document->at("gate_object").get<string>();
							if (!isSha256(gateHash)) throw runtime_error("bad hash");
							entry.referencedObjects.push_back(gateObjectPath(cacheRoot, gateHash));
						}
						scan.inventory.cachedCompositions += document->at("composition_count").get<uint64_t>();
						++scan.inventory.compositionManifests;
					}
					else {
						throw runtime_error("wrong manifest kind");
					}
					for (const filesystem::path& referenced : entry.referencedObjects) {
						scan.reachableObjects.insert(normalizedPathKey(referenced));
					}
					scan.manifests.push_back(move(entry));
				}
				catch (const exception&) {
					++scan.inventory.invalidManifests;
					scan.invalidManifests.push_back(path);
				}
				continue;
			}

			if (path.parent_path() == gateObjectDirectory || path.parent_path() == compositionObjectDirectory) {
				scan.objectFiles.push_back(path);
				scan.inventory.objectBytes += size;
				if (path.parent_path() == gateObjectDirectory) ++scan.inventory.gateObjects;
				else ++scan.inventory.compositionObjects;
			}
		}

		for (const filesystem::path& objectPath : scan.objectFiles) {
			if (scan.reachableObjects.contains(normalizedPathKey(objectPath))) continue;
			++scan.inventory.orphanedObjects;
			scan.inventory.orphanedBytes += safeFileSize(objectPath);
		}
		return scan;
	}

	void removeTrackedFile(const filesystem::path& path, CompositionCachePruneResult& result) {
		uintmax_t size = safeFileSize(path);
		error_code error;
		bool removed = filesystem::remove(path, error);
		if (removed && !error) {
			++result.removedFiles;
			result.removedBytes += size;
		}
	}

	void removeOrphanObjects(const filesystem::path& cacheRoot, CompositionCachePruneResult& result) {
		CacheScan scan = scanCache(cacheRoot);
		for (const filesystem::path& objectPath : scan.objectFiles) {
			if (!scan.reachableObjects.contains(normalizedPathKey(objectPath))) removeTrackedFile(objectPath, result);
		}
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
	TeamComposition seedComposition = makeSeedComposition(request.emblemTraits);
	if (!request.useCache) return calculateWithoutCache(request, seedComposition);

	optional<CacheKeys> keys;
	try {
		keys = makeCacheKeys(request);
	}
	catch (const exception& error) {
		warnCacheFailure("create the cache identity", error);
		return calculateWithoutCache(request, seedComposition);
	}

	if (!request.refreshCache) {
		if (optional<vector<TeamComposition>> cachedCompositions = readCompositionCache(request, *keys, seedComposition)) {
			return { move(*cachedCompositions), CompositionResolution::CompositionCacheHit };
		}
	}
	else {
		try {
			invalidateCompositionManifest(request, *keys);
		}
		catch (const exception& error) {
			warnCacheFailure("invalidate the refreshed composition", error);
		}
	}

	optional<CachedGateRecord> cachedGates;
	if (!request.refreshCache) cachedGates = readGateCache(request, *keys);
	if (cachedGates.has_value() && cachedGates->record.calculatedSizes[request.compositionSize - 1]) {
		TeamComposition::setGateTable(cachedGates->record.table);
		int settings[3] = { 0, request.gateType == GateType::ActiveTraitTiers ? 1 : 0, request.connectedChampsOnly ? 1 : 0 };
		vector<TeamComposition> compositions = TeamComposition::generateComps(request.compositionSize, settings, seedComposition);
		string rowFingerprint = gateRowFingerprint(cachedGates->record.table, request.gateType, request.compositionSize);
		try {
			writeCompositionCache(request, *keys, rowFingerprint, cachedGates->objectHash, compositions);
		}
		catch (const exception& error) {
			warnCacheFailure("save compositions", error);
		}
		return { move(compositions), CompositionResolution::GateCacheHit };
	}

	GateCacheRecord updatedGates = cachedGates.has_value() ? cachedGates->record : GateCacheRecord{};
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

	optional<string> gateObjectHash;
	try {
		gateObjectHash = writeGateCache(request, *keys, updatedGates);
	}
	catch (const exception& error) {
		warnCacheFailure("save gates", error);
	}

	int settings[3] = { 0, request.gateType == GateType::ActiveTraitTiers ? 1 : 0, request.connectedChampsOnly ? 1 : 0 };
	vector<TeamComposition> compositions = TeamComposition::generateComps(request.compositionSize, settings, seedComposition);
	string rowFingerprint = gateRowFingerprint(calculatedTable, request.gateType, request.compositionSize);
	try {
		writeCompositionCache(request, *keys, rowFingerprint, gateObjectHash, compositions);
	}
	catch (const exception& error) {
		warnCacheFailure("save compositions", error);
	}
	return { move(compositions), CompositionResolution::CalculatedGates };
}

CompositionCacheInventory inspectCompositionCache(const filesystem::path& cacheRoot) {
	return scanCache(cacheRoot).inventory;
}

CompositionCachePruneResult pruneCompositionCache(
	const filesystem::path& cacheRoot,
	optional<uintmax_t> maximumBytes
) {
	CompositionCachePruneResult result;
	result.before = inspectCompositionCache(cacheRoot);
	if (result.before.totalFiles == 0) {
		result.after = result.before;
		return result;
	}

	CacheWriteLock lock(cacheRoot);
	CacheScan scan = scanCache(cacheRoot);
	for (const filesystem::path& path : scan.temporaryFiles) removeTrackedFile(path, result);
	for (const filesystem::path& path : scan.invalidManifests) removeTrackedFile(path, result);
	for (const filesystem::path& path : scan.legacyFiles) removeTrackedFile(path, result);
	removeOrphanObjects(cacheRoot, result);

	if (maximumBytes.has_value()) {
		CacheScan current = scanCache(cacheRoot);
		sort(current.manifests.begin(), current.manifests.end(), [](const ManifestEntry& left, const ManifestEntry& right) {
			return left.lastWriteTime < right.lastWriteTime;
		});
		for (const ManifestEntry& manifest : current.manifests) {
			if (inspectCompositionCache(cacheRoot).totalBytes <= *maximumBytes) break;
			removeTrackedFile(manifest.path, result);
			removeOrphanObjects(cacheRoot, result);
		}
	}

	removeOrphanObjects(cacheRoot, result);
	result.after = inspectCompositionCache(cacheRoot);
	return result;
}
