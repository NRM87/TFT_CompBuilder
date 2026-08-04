#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ThirdParty/cpp-httplib/httplib.h"
#include <windows.h>
#include <shellapi.h>

#include "LocalWebServer.h"

#include "Champion.h"
#include "CompBuilderUtils.h"
#include "CompositionCache.h"
#include "TeamComposition.h"
#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ws2_32.lib")

using json = nlohmann::json;
using namespace std;

namespace {
	struct BuildOptions {
		string setNumber;
		int compositionSize = 9;
		GateType gateType = GateType::ActiveTraits;
		vector<string> emblemTraits;
		bool connectedChampsOnly = false;
		int gateTimeoutSeconds = 10;
		bool useCache = true;
		bool refreshCache = false;
	};

	struct CompositionSummary {
		vector<string> champions;
		int activeTraits = 0;
		int activeTraitTiers = 0;
		int boardWidth = 0;
	};

	struct JobRecord {
		uint64_t id = 0;
		string status = "idle";
		string message;
		BuildOptions options;
		string resolution;
		double elapsedSeconds = 0.0;
		chrono::steady_clock::time_point startedAt{};
		vector<CompositionSummary> compositions;
	};

	bool isValidSetName(const string& set) {
		if (set.empty()) return false;
		bool foundDecimalPoint = false;
		for (size_t i = 0; i < set.size(); ++i) {
			unsigned char character = static_cast<unsigned char>(set[i]);
			if (isdigit(character)) continue;
			if (set[i] == '.' && !foundDecimalPoint && i > 0 && i + 1 < set.size()) {
				foundDecimalPoint = true;
				continue;
			}
			return false;
		}
		return true;
	}

	filesystem::path setDirectory(const string& setNumber) {
		return filesystem::path("SetInfos") / ("Set" + setNumber);
	}

	filesystem::path championInfoPath(const string& setNumber) {
		return setDirectory(setNumber) / "ChampionInfo.txt";
	}

	filesystem::path traitInfoPath(const string& setNumber) {
		return setDirectory(setNumber) / "TraitInfo.txt";
	}

	void setJsonResponse(httplib::Response& response, const json& document, int status = 200) {
		response.status = status;
		response.set_content(document.dump(), "application/json; charset=utf-8");
		response.set_header("Cache-Control", "no-store");
	}

	void setErrorResponse(httplib::Response& response, int status, const string& message) {
		setJsonResponse(response, { { "error", message } }, status);
	}

	json cacheInventoryJson(const CompositionCacheInventory& inventory) {
		return {
			{ "total_bytes", inventory.totalBytes },
			{ "total_files", inventory.totalFiles },
			{ "object_bytes", inventory.objectBytes },
			{ "gate_manifests", inventory.gateManifests },
			{ "composition_manifests", inventory.compositionManifests },
			{ "gate_objects", inventory.gateObjects },
			{ "composition_objects", inventory.compositionObjects },
			{ "cached_compositions", inventory.cachedCompositions },
			{ "orphaned_objects", inventory.orphanedObjects },
			{ "orphaned_bytes", inventory.orphanedBytes },
			{ "invalid_manifests", inventory.invalidManifests },
			{ "temporary_files", inventory.temporaryFiles },
			{ "legacy_files", inventory.legacyFiles },
			{ "legacy_bytes", inventory.legacyBytes }
		};
	}

	json readSetCatalog() {
		filesystem::path root = "SetInfos";
		if (!filesystem::is_directory(root)) throw runtime_error("Could not find the SetInfos directory.");

		vector<pair<double, json>> orderedSets;
		for (const filesystem::directory_entry& entry : filesystem::directory_iterator(root)) {
			if (!entry.is_directory()) continue;
			string directoryName = entry.path().filename().string();
			if (!directoryName.starts_with("Set")) continue;
			string setNumber = directoryName.substr(3);
			if (!isValidSetName(setNumber)) continue;

			filesystem::path championPath = entry.path() / "ChampionInfo.txt";
			filesystem::path traitPath = entry.path() / "TraitInfo.txt";
			if (!filesystem::is_regular_file(championPath) || !filesystem::is_regular_file(traitPath)) continue;

			try {
				unordered_map<string, Champion> champions;
				unordered_map<string, vector<int>> traits;
				readChampInfo(championPath.string(), champions);
				readTraitInfo(traitPath.string(), traits);
				validateSetData(champions, traits);

				vector<string> traitNames;
				traitNames.reserve(traits.size());
				for (const auto& [name, milestones] : traits) traitNames.push_back(name);
				sort(traitNames.begin(), traitNames.end());
				orderedSets.push_back({
					stod(setNumber),
					{
						{ "id", setNumber },
						{ "champion_count", champions.size() },
						{ "trait_count", traits.size() },
						{ "traits", move(traitNames) }
					}
				});
			}
			catch (const exception&) {
				// Incomplete historical set directories are not offered by the web UI.
			}
		}

		sort(orderedSets.begin(), orderedSets.end(), [](const auto& left, const auto& right) {
			return left.first > right.first;
		});
		json sets = json::array();
		for (auto& [numericValue, document] : orderedSets) sets.push_back(move(document));
		return {
			{ "latest", sets.empty() ? json(nullptr) : sets.front().at("id") },
			{ "sets", move(sets) }
		};
	}

	BuildOptions parseBuildOptions(const json& document) {
		BuildOptions options;
		options.setNumber = document.at("set").get<string>();
		if (!isValidSetName(options.setNumber) || !filesystem::is_directory(setDirectory(options.setNumber))) {
			throw runtime_error("Unknown set \"" + options.setNumber + "\".");
		}
		options.compositionSize = document.value("size", 9);
		if (options.compositionSize < 1 || options.compositionSize > GateTable::SIZE) {
			throw runtime_error("Composition size must be between 1 and 10.");
		}
		string gateType = document.value("gate_type", "traits");
		if (gateType == "traits") options.gateType = GateType::ActiveTraits;
		else if (gateType == "tiers") options.gateType = GateType::ActiveTraitTiers;
		else throw runtime_error("Gate type must be \"traits\" or \"tiers\".");

		if (document.contains("emblems")) options.emblemTraits = document.at("emblems").get<vector<string>>();
		options.connectedChampsOnly = document.value("connected_only", false);
		options.gateTimeoutSeconds = document.value("gate_timeout", 10);
		if (options.gateTimeoutSeconds < 1 || options.gateTimeoutSeconds > 3600) {
			throw runtime_error("Gate timeout must be between 1 and 3600 seconds.");
		}
		options.useCache = document.value("use_cache", true);
		options.refreshCache = document.value("refresh", false);
		return options;
	}

	class JobManager {
	public:
		~JobManager() {
			if (worker.joinable()) worker.join();
		}

		bool start(BuildOptions options, uint64_t& jobId, string& rejection) {
			lock_guard startGuard(startMutex);
			{
				lock_guard guard(stateMutex);
				if (current.has_value() && (current->status == "queued" || current->status == "running")) {
					rejection = "A composition calculation is already running.";
					return false;
				}
			}
			if (worker.joinable()) worker.join();

			{
				lock_guard guard(stateMutex);
				jobId = nextJobId++;
				current = JobRecord{};
				current->id = jobId;
				current->status = "queued";
				current->message = "Waiting for the calculation worker.";
				current->options = options;
			}
			worker = thread([this, jobId, options = move(options)] { run(jobId, options); });
			return true;
		}

		bool isActive() const {
			lock_guard guard(stateMutex);
			return current.has_value() && (current->status == "queued" || current->status == "running");
		}

		optional<json> snapshot(uint64_t jobId, size_t offset, size_t limit) const {
			lock_guard guard(stateMutex);
			if (!current.has_value() || current->id != jobId) return nullopt;

			double elapsed = current->elapsedSeconds;
			if (current->status == "running") {
				elapsed = chrono::duration<double>(chrono::steady_clock::now() - current->startedAt).count();
			}
			json document = {
				{ "id", current->id },
				{ "status", current->status },
				{ "message", current->message },
				{ "elapsed_seconds", elapsed },
				{ "resolution", current->resolution.empty() ? json(nullptr) : json(current->resolution) },
				{ "total", current->compositions.size() },
				{ "offset", offset },
				{ "limit", limit },
				{ "request", {
					{ "set", current->options.setNumber },
					{ "size", current->options.compositionSize },
					{ "gate_type", gateTypeName(current->options.gateType) },
					{ "emblems", current->options.emblemTraits },
					{ "connected_only", current->options.connectedChampsOnly }
				} }
			};

			json results = json::array();
			if (current->status == "complete" && offset < current->compositions.size()) {
				size_t end = min(current->compositions.size(), offset + limit);
				for (size_t index = offset; index < end; ++index) {
					const CompositionSummary& composition = current->compositions[index];
					results.push_back({
						{ "champions", composition.champions },
						{ "active_traits", composition.activeTraits },
						{ "active_trait_tiers", composition.activeTraitTiers },
						{ "board_width", composition.boardWidth }
					});
				}
			}
			document["results"] = move(results);
			return document;
		}

	private:
		void run(uint64_t jobId, const BuildOptions& options) {
			auto startedAt = chrono::steady_clock::now();
			{
				lock_guard guard(stateMutex);
				if (!current.has_value() || current->id != jobId) return;
				current->status = "running";
				current->message = "Loading the set and resolving cached or calculated gates.";
				current->startedAt = startedAt;
			}

			try {
				unordered_map<string, Champion> champions;
				unordered_map<string, vector<int>> traits;
				readChampInfo(championInfoPath(options.setNumber).string(), champions);
				readTraitInfo(traitInfoPath(options.setNumber).string(), traits);
				validateSetData(champions, traits);
				TeamComposition::initializeStatics(traits, champions);

				GatedCompositionRequest request;
				request.setNumber = options.setNumber;
				request.championInfoFile = championInfoPath(options.setNumber);
				request.traitInfoFile = traitInfoPath(options.setNumber);
				request.compositionSize = options.compositionSize;
				request.gateType = options.gateType;
				request.emblemTraits = options.emblemTraits;
				request.connectedChampsOnly = options.connectedChampsOnly;
				request.gateTimeoutSeconds = options.gateTimeoutSeconds;
				request.useCache = options.useCache;
				request.refreshCache = options.refreshCache;

				GatedCompositionResult result = getOrCalculateGatedCompositions(request);
				if (options.setNumber == "12") set12TahmFilter(result.compositions);

				vector<CompositionSummary> summaries;
				summaries.reserve(result.compositions.size());
				for (const TeamComposition& composition : result.compositions) {
					summaries.push_back({
						composition.getChampionLabels(),
						composition.getActiveTraitsTotal(),
						composition.getActiveTraitTiersTotal(),
						composition.size()
					});
				}

				lock_guard guard(stateMutex);
				if (!current.has_value() || current->id != jobId) return;
				current->status = "complete";
				current->message = "Composition search complete.";
				current->resolution = compositionResolutionName(result.resolution);
				current->elapsedSeconds = chrono::duration<double>(chrono::steady_clock::now() - startedAt).count();
				current->compositions = move(summaries);
			}
			catch (const exception& error) {
				lock_guard guard(stateMutex);
				if (!current.has_value() || current->id != jobId) return;
				current->status = "failed";
				current->message = error.what();
				current->elapsedSeconds = chrono::duration<double>(chrono::steady_clock::now() - startedAt).count();
			}
		}

		mutable mutex stateMutex;
		mutex startMutex;
		optional<JobRecord> current;
		thread worker;
		uint64_t nextJobId = 1;
	};

	uint64_t parseJobId(const httplib::Request& request) {
		return stoull(request.matches[1].str());
	}

	size_t parsePageParameter(const httplib::Request& request, const char* name, size_t fallback, size_t maximum) {
		if (!request.has_param(name)) return fallback;
		size_t parsedCharacters = 0;
		unsigned long long value = stoull(request.get_param_value(name), &parsedCharacters);
		if (parsedCharacters != request.get_param_value(name).size() || value > maximum) {
			throw runtime_error(string("Invalid ") + name + " query parameter.");
		}
		return static_cast<size_t>(value);
	}
}

void runLocalWebServer(int port, const filesystem::path& webRoot, bool openBrowser) {
	if (port < 1 || port > 65535) throw runtime_error("Web server port must be between 1 and 65535.");
	if (!filesystem::is_regular_file(webRoot / "index.html")) {
		throw runtime_error("Could not find the local web frontend at " + (webRoot / "index.html").string() + ".");
	}

	JobManager jobs;
	httplib::Server server;
	server.set_payload_max_length(1024 * 1024);

	server.Get("/api/sets", [](const httplib::Request&, httplib::Response& response) {
		try {
			setJsonResponse(response, readSetCatalog());
		}
		catch (const exception& error) {
			setErrorResponse(response, 500, error.what());
		}
	});

	server.Post("/api/jobs", [&jobs](const httplib::Request& request, httplib::Response& response) {
		try {
			BuildOptions options = parseBuildOptions(json::parse(request.body));
			uint64_t jobId = 0;
			string rejection;
			if (!jobs.start(move(options), jobId, rejection)) {
				setErrorResponse(response, 409, rejection);
				return;
			}
			setJsonResponse(response, { { "id", jobId }, { "status", "queued" } }, 202);
		}
		catch (const exception& error) {
			setErrorResponse(response, 400, error.what());
		}
	});

	server.Get(R"(/api/jobs/(\d+))", [&jobs](const httplib::Request& request, httplib::Response& response) {
		try {
			uint64_t jobId = parseJobId(request);
			size_t offset = parsePageParameter(request, "offset", 0, numeric_limits<size_t>::max());
			size_t limit = parsePageParameter(request, "limit", 100, 500);
			optional<json> snapshot = jobs.snapshot(jobId, offset, limit);
			if (!snapshot.has_value()) {
				setErrorResponse(response, 404, "Unknown composition job.");
				return;
			}
			setJsonResponse(response, *snapshot);
		}
		catch (const exception& error) {
			setErrorResponse(response, 400, error.what());
		}
	});

	server.Get("/api/cache", [](const httplib::Request&, httplib::Response& response) {
		try {
			setJsonResponse(response, cacheInventoryJson(inspectCompositionCache("Cache")));
		}
		catch (const exception& error) {
			setErrorResponse(response, 500, error.what());
		}
	});

	server.Post("/api/cache/prune", [&jobs](const httplib::Request& request, httplib::Response& response) {
		try {
			if (jobs.isActive()) {
				setErrorResponse(response, 409, "Wait for the active composition calculation before pruning the cache.");
				return;
			}
			optional<uintmax_t> maximumBytes;
			if (!request.body.empty()) {
				json document = json::parse(request.body);
				if (document.contains("maximum_mb") && !document.at("maximum_mb").is_null()) {
					long long maximumMegabytes = document.at("maximum_mb").get<long long>();
					if (maximumMegabytes < 0) throw runtime_error("Cache maximum cannot be negative.");
					if (static_cast<uintmax_t>(maximumMegabytes) > numeric_limits<uintmax_t>::max() / (1024 * 1024)) {
						throw runtime_error("Cache maximum is too large.");
					}
					maximumBytes = static_cast<uintmax_t>(maximumMegabytes) * 1024 * 1024;
				}
			}
			CompositionCachePruneResult result = pruneCompositionCache("Cache", maximumBytes);
			setJsonResponse(response, {
				{ "removed_files", result.removedFiles },
				{ "removed_bytes", result.removedBytes },
				{ "cache", cacheInventoryJson(result.after) }
			});
		}
		catch (const exception& error) {
			setErrorResponse(response, 400, error.what());
		}
	});

	if (!server.set_mount_point("/", filesystem::absolute(webRoot).string())) {
		throw runtime_error("Could not mount the local web frontend directory.");
	}

	int boundPort = server.bind_to_port("127.0.0.1", port);
	if (boundPort < 0) throw runtime_error("Could not bind the local web server to 127.0.0.1:" + to_string(port) + ".");
	string url = "http://127.0.0.1:" + to_string(boundPort) + "/";
	cout << "TFT Comp Builder web interface: " << url << endl;
	cout << "Press Ctrl+C to stop the local server." << endl;
	if (openBrowser) {
		HINSTANCE result = ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		if (reinterpret_cast<intptr_t>(result) <= 32) {
			cerr << "Could not open the default browser automatically. Open " << url << " manually." << endl;
		}
	}
	if (!server.listen_after_bind()) throw runtime_error("The local web server stopped unexpectedly.");
}
