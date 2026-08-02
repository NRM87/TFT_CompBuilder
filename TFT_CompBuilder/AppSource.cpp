#include <string>
#include <vector>
#include <iostream>
#include <iomanip> 
#include <fstream>
#include <sstream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <ctime>
#include <chrono>
#include <optional>
#include <cctype>
#include <filesystem>
#include <limits>
#include "Champion.h"
#include "CompBuilderUtils.h"
#include "CompositionCache.h"
#include "TeamComposition.h"
using namespace std;

#define CDRAGONJSON(set) ("TFTSetJSONs\\set" + set + ".json")
#define SETINFODIR(set) ("SetInfos\\Set" + set)
#define CHAMPINFOFILE(set) (SETINFODIR(set) + "\\ChampionInfo.txt")
#define TRAITINFOFILE(set) (SETINFODIR(set) + "\\TraitInfo.txt")
#define DEFAULT_GATE_TIMEOUT_SECONDS 10

namespace {
	struct CommandLineOptions {
		optional<string> set;
		optional<string> inputPath;
		int compositionSize = 9;
		bool compositionSizeSpecified = false;
		GateType gateType = GateType::ActiveTraits;
		vector<string> emblemTraits;
		bool connectedChampsOnly = false;
		int gateTimeoutSeconds = DEFAULT_GATE_TIMEOUT_SECONDS;
		bool refreshCache = false;
		bool useCache = true;
		bool showCacheInfo = false;
		bool pruneCache = false;
		optional<uintmax_t> cacheMaximumBytes;
		bool interactive = false;
		bool showHelp = false;
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

	string readOptionValue(int& argumentIndex, int argumentCount, char* arguments[], const string& option) {
		if (argumentIndex + 1 >= argumentCount) {
			throw runtime_error("Missing value for " + option + ".");
		}
		return arguments[++argumentIndex];
	}

	int parseIntegerOption(const string& value, const string& option, int minimum, int maximum) {
		size_t parsedCharacters = 0;
		int parsedValue = 0;
		try {
			parsedValue = stoi(value, &parsedCharacters);
		}
		catch (const exception&) {
			throw runtime_error("Invalid value for " + option + ": " + value + ".");
		}
		if (parsedCharacters != value.size() || parsedValue < minimum || parsedValue > maximum) {
			throw runtime_error(
				"Invalid value for " + option + ": " + value + ". Expected an integer between " +
				to_string(minimum) + " and " + to_string(maximum) + "."
			);
		}
		return parsedValue;
	}

	GateType parseGateType(const string& value) {
		if (value == "traits") return GateType::ActiveTraits;
		if (value == "tiers") return GateType::ActiveTraitTiers;
		throw runtime_error("Invalid gate type \"" + value + "\". Expected traits or tiers.");
	}

	CommandLineOptions parseCommandLine(int argumentCount, char* arguments[]) {
		CommandLineOptions options;
		for (int i = 1; i < argumentCount; ++i) {
			string argument = arguments[i];
			if (argument == "--help" || argument == "-h") {
				options.showHelp = true;
			}
			else if (argument == "--set" || argument == "-s") {
				options.set = readOptionValue(i, argumentCount, arguments, argument);
			}
			else if (argument.starts_with("--set=")) {
				options.set = argument.substr(6);
			}
			else if (argument == "--input" || argument == "-i") {
				options.inputPath = readOptionValue(i, argumentCount, arguments, argument);
			}
			else if (argument.starts_with("--input=")) {
				options.inputPath = argument.substr(8);
			}
			else if (argument == "--size") {
				options.compositionSize = parseIntegerOption(readOptionValue(i, argumentCount, arguments, argument), argument, 1, 10);
				options.compositionSizeSpecified = true;
			}
			else if (argument.starts_with("--size=")) {
				options.compositionSize = parseIntegerOption(argument.substr(7), "--size", 1, 10);
				options.compositionSizeSpecified = true;
			}
			else if (argument == "--gate-type") {
				options.gateType = parseGateType(readOptionValue(i, argumentCount, arguments, argument));
			}
			else if (argument.starts_with("--gate-type=")) {
				options.gateType = parseGateType(argument.substr(12));
			}
			else if (argument == "--emblem") {
				options.emblemTraits.push_back(readOptionValue(i, argumentCount, arguments, argument));
			}
			else if (argument.starts_with("--emblem=")) {
				options.emblemTraits.push_back(argument.substr(9));
			}
			else if (argument == "--connected-only") {
				options.connectedChampsOnly = true;
			}
			else if (argument == "--gate-timeout") {
				options.gateTimeoutSeconds = parseIntegerOption(
					readOptionValue(i, argumentCount, arguments, argument), argument, 1, numeric_limits<int>::max()
				);
			}
			else if (argument.starts_with("--gate-timeout=")) {
				options.gateTimeoutSeconds = parseIntegerOption(argument.substr(15), "--gate-timeout", 1, numeric_limits<int>::max());
			}
			else if (argument == "--refresh") {
				options.refreshCache = true;
			}
			else if (argument == "--no-cache") {
				options.useCache = false;
			}
			else if (argument == "--cache-info") {
				options.showCacheInfo = true;
			}
			else if (argument == "--cache-prune") {
				options.pruneCache = true;
			}
			else if (argument == "--cache-max-mb") {
				uintmax_t megabytes = static_cast<uintmax_t>(parseIntegerOption(
					readOptionValue(i, argumentCount, arguments, argument), argument, 0, numeric_limits<int>::max()
				));
				options.cacheMaximumBytes = megabytes * 1024 * 1024;
			}
			else if (argument.starts_with("--cache-max-mb=")) {
				uintmax_t megabytes = static_cast<uintmax_t>(parseIntegerOption(
					argument.substr(15), "--cache-max-mb", 0, numeric_limits<int>::max()
				));
				options.cacheMaximumBytes = megabytes * 1024 * 1024;
			}
			else if (argument == "--interactive") {
				options.interactive = true;
			}
			else {
				throw runtime_error("Unknown command-line option: " + argument + ". Run with --help for usage.");
			}
		}

		if (options.set.has_value() && !isValidSetName(*options.set)) {
			throw runtime_error("Invalid set name \"" + *options.set + "\". Expected a number such as 18 or 8.5.");
		}
		if (options.inputPath.has_value() && options.inputPath->empty()) {
			throw runtime_error("--input requires a non-empty file path or '-' for standard input.");
		}
		if (options.cacheMaximumBytes.has_value() && !options.pruneCache) {
			throw runtime_error("--cache-max-mb must be used with --cache-prune.");
		}
		if (options.inputPath.has_value()) options.interactive = true;
		return options;
	}

	string formatBytes(uintmax_t bytes) {
		static const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
		double amount = static_cast<double>(bytes);
		size_t unit = 0;
		while (amount >= 1024.0 && unit + 1 < size(units)) {
			amount /= 1024.0;
			++unit;
		}
		ostringstream output;
		output << fixed << setprecision(unit == 0 ? 0 : 2) << amount << ' ' << units[unit];
		return output.str();
	}

	void printCacheInventory(const CompositionCacheInventory& inventory) {
		cout
			<< "Cache size:              " << formatBytes(inventory.totalBytes) << " in " << inventory.totalFiles << " files\n"
			<< "Gate manifests/objects:  " << inventory.gateManifests << " / " << inventory.gateObjects << '\n'
			<< "Comp manifests/objects:  " << inventory.compositionManifests << " / " << inventory.compositionObjects << '\n'
			<< "Cached compositions:     " << inventory.cachedCompositions << '\n'
			<< "Object data:             " << formatBytes(inventory.objectBytes) << '\n'
			<< "Orphaned objects:        " << inventory.orphanedObjects << " (" << formatBytes(inventory.orphanedBytes) << ")\n"
			<< "Invalid manifests:       " << inventory.invalidManifests << '\n'
			<< "Temporary files:         " << inventory.temporaryFiles << '\n'
			<< "Legacy cache files:      " << inventory.legacyFiles << " (" << formatBytes(inventory.legacyBytes) << ")\n";
	}

	string findLatestSet() {
		const filesystem::path setInfoDirectory = "SetInfos";
		if (!filesystem::is_directory(setInfoDirectory)) {
			throw runtime_error("Could not find the SetInfos directory.");
		}

		optional<string> latestSet;
		double latestValue = -1.0;
		for (const filesystem::directory_entry& entry : filesystem::directory_iterator(setInfoDirectory)) {
			if (!entry.is_directory()) continue;
			string directoryName = entry.path().filename().string();
			if (!directoryName.starts_with("Set")) continue;
			string candidate = directoryName.substr(3);
			if (!isValidSetName(candidate)) continue;
			if (!filesystem::exists(entry.path() / "ChampionInfo.txt") || !filesystem::exists(entry.path() / "TraitInfo.txt")) continue;
			double candidateValue = stod(candidate);
			if (!latestSet.has_value() || candidateValue > latestValue) {
				latestSet = candidate;
				latestValue = candidateValue;
			}
		}
		if (!latestSet.has_value()) throw runtime_error("Could not find any valid SetInfos\\Set<number> directory.");
		return *latestSet;
	}

	void printUsage() {
		cout
			<< "Usage: TFT_CompBuilder.exe [options]\n\n"
			<< "Options:\n"
			<< "  -s, --set <number>       TFT set to load (default: latest available set).\n"
			<< "      --size <1-10>        Team composition size (default: 9).\n"
			<< "      --gate-type <type>   traits (default) or tiers.\n"
			<< "      --emblem <trait>     Add an emblem; repeat for multiple or duplicate emblems.\n"
			<< "      --connected-only     Only extend comps with connected champions.\n"
			<< "      --gate-timeout <s>   Gate probe timeout in seconds (default: 10).\n"
			<< "      --refresh            Ignore matching caches and recalculate gates.\n"
			<< "      --no-cache           Do not read or write the disk cache.\n"
			<< "      --cache-info         Show cache usage and exit.\n"
			<< "      --cache-prune        Remove stale, invalid, and orphaned cache data, then exit.\n"
			<< "      --cache-max-mb <n>   With --cache-prune, evict least-recently-used entries to this limit.\n"
			<< "      --interactive        Use the legacy prompt-driven setup flow.\n"
			<< "  -i, --input <path>       Read interactive answers from a file; implies --interactive.\n"
			<< "  -h, --help               Show this help message.\n\n"
			<< "With no options, the latest available set is selected dynamically and size-9 trait gates are used.\n"
			<< "Run the executable from the project directory so SetInfos and Cache can be found.\n";
	}
}

int main(int argc, char* argv[]) {
	try {
		CommandLineOptions options = parseCommandLine(argc, argv);
		if (options.showHelp) {
			printUsage();
			return 0;
		}
		if (options.pruneCache) {
			CompositionCachePruneResult result = pruneCompositionCache("Cache", options.cacheMaximumBytes);
			cout << "Cache pruning complete: removed " << result.removedFiles << " files ("
				<< formatBytes(result.removedBytes) << ").\n";
			printCacheInventory(result.after);
			return 0;
		}
		if (options.showCacheInfo) {
			printCacheInventory(inspectCompositionCache("Cache"));
			return 0;
		}

		string set = options.set.value_or(findLatestSet());
		ifstream inputFile;
		istream* input = &cin;
		string inputDescription = "standard input";
		if (options.inputPath.has_value() && *options.inputPath != "-") {
			inputFile.open(*options.inputPath);
			if (!inputFile.is_open()) {
				throw runtime_error("Could not open input file: " + *options.inputPath);
			}
			input = &inputFile;
			inputDescription = "input file \"" + *options.inputPath + "\"";
		}
		istream& in = *input;
		auto readToken = [&](const string& context) -> string {
			string value;
			if (!(in >> value)) {
				throw runtime_error("Failed reading " + context + " from " + inputDescription + ".");
			}
			return value;
		};
		auto readInt = [&](const string& context) -> int {
			int value = 0;
			if (!(in >> value)) {
				throw runtime_error("Failed reading integer for " + context + " from " + inputDescription + ".");
			}
			return value;
		};

		cout << "Welcome to the Set " << set << " team composition generator!" << endl;
		string ans;
		if (options.interactive) {
			cout << "Would you like to update the current set information by parsing the CDragon JSON (\"y\"/\"n\")? (If info text files are not up to date.)" << endl;
			ans = readToken("set update choice");
			if (ans == "y") {
				bool canParseCDragon = stoi(set) >= 8 && set != "8.5";
				if (canParseCDragon) {
					cout << "Parsing cdragon json..." << endl;
					parseCDragon(CDRAGONJSON(set), set);
					cout << "CDragon parsing complete." << endl;
				}
				else {
					cout << "CDragon parsing is not supported for Set " << set << ". Using the existing set files." << endl;
				}
			}
		}

		//Initialize Champions
		unordered_map<string,Champion> setChamps; //map of champion names and corresponding Champion object
		readChampInfo(CHAMPINFOFILE(set), setChamps); //fills setChamps with info from SetChampionInfo text file

		int count = 1;
		if (options.interactive) {
			cout << "Champions:" << endl;
			for (const pair<const string, Champion>& champ : setChamps) {
				cout << setw(2) << count << ". " << champ.second.toString() << endl;
				count++;
			}
		}
		
		//Initialize Traits
		unordered_map<string, vector<int>> setTraits; //map of trait names and their corresponding milestone values
		readTraitInfo(TRAITINFOFILE(set), setTraits); //fills setTraits with info from SetTraitInfo text file
		validateSetData(setChamps, setTraits);

		if (options.interactive) {
			cout << endl << "Traits:" << endl;
			count = 1;
			for (unordered_map<string, vector<int>>::const_iterator it = setTraits.begin(); it != setTraits.end(); ++it) {
				cout << right << setw(2) << count << ". " << left << setw(15) << it->first << " (";
				if (!it->second.empty()) cout << it->second.at(0);
				for (int i = 1; i < it->second.size(); ++i) cout << "/" << it->second.at(i);
				cout << ")" << endl;
				++count;
			}
		}

		cout << "Initializing Set " << set << " data..." << endl;
		TeamComposition::initializeStatics(setTraits, setChamps); //Uses data from the trait and champ maps to initialize TeamCompositions to be constructed and used
		TeamComposition seedComp;
		vector<string> emblemTraits = options.emblemTraits;
		if (options.interactive) {
			unordered_map<string, vector<string>> championGraph = TeamComposition::getChampGraph();
			cout << endl << "Champion Graph:" << endl;
			count = 1;
			for (const auto& [champion, connectedChampions] : championGraph) {
				cout << right << setw(2) << count << ". " << left << setw(16) << (champion + ":");
				string temp = "[";
				for (size_t i = 0; i < connectedChampions.size(); ++i) {
					if (i > 0) temp += ", ";
					temp += connectedChampions[i];
				}
				temp += "]";
				cout << left << setw(128) << temp << right << setw(2) << connectedChampions.size() << " connected champions." << endl;
				++count;
			}

			cout << endl << "Do you have additional emblems to add? (\"y\"/\"n\")." << endl;
			ans = readToken("emblem choice");
			if (ans == "y") {
				cout << "Enter a space separated list of trait names to add as emblems." << endl;
				string emblemLine;
				if (!getline(in >> ws, emblemLine)) {
					throw runtime_error("Failed reading emblem trait list from " + inputDescription + ".");
				}
				istringstream emblemStream(emblemLine);
				string emblemTrait;
				bool foundEmblem = false;
				while (emblemStream >> emblemTrait) {
					emblemTraits.push_back(emblemTrait);
					foundEmblem = true;
				}
				if (!foundEmblem) throw runtime_error("No emblem traits were provided.");
			}
		}
		for (const string& emblemTrait : emblemTraits) seedComp.incrementTrait(emblemTrait);

		int settings[3] = {
			0,
			options.gateType == GateType::ActiveTraitTiers ? 1 : 0,
			options.connectedChampsOnly ? 1 : 0
		};
		if (options.interactive) {
			cout << endl << "Do you want to change the default settings? (\"y\"/\"n\")." << endl;
			ans = readToken("settings change choice");
			if (ans == "y") {
				cout << "Enter 3 numbers, each either 0 or 1, separated by a space." << endl;
				cout << "First: use trait gates (0) or no gates (1)." << endl;
				cout << "Second: count unique active traits (0) or active trait tiers (1)." << endl;
				cout << "Third: consider all champions (0) or only connected champions (1)." << endl;
				for (int i = 0; i < 3; ++i) {
					settings[i] = readInt("settings[" + to_string(i) + "]");
					if (settings[i] != 0 && settings[i] != 1) {
						throw runtime_error("Invalid value for settings[" + to_string(i) + "]. Expected 0 or 1.");
					}
				}
			}
		}

		int compositionSize = options.compositionSize;
		if (options.interactive && !options.compositionSizeSpecified) {
			cout << endl << "What size comp would you like to find? (Enter an integer between 1 and 10; default automation size is 9): ";
			compositionSize = readInt("composition size");
		}
		if (compositionSize < 1 || compositionSize > 10) {
			throw runtime_error("Composition size must be between 1 and 10.");
		}

		cout << endl << "Resolving size-" << compositionSize << " compositions";
		if (settings[0] == 0) {
			cout << " with " << gateTypeName(settings[1] == 1 ? GateType::ActiveTraitTiers : GateType::ActiveTraits) << " gates";
		}
		else {
			cout << " without gate pruning";
		}
		cout << "..." << endl;
		auto programStartTime = chrono::steady_clock::now();
		vector<TeamComposition> compList;
		if (settings[0] == 0) {
			GatedCompositionRequest request;
			request.setNumber = set;
			request.championInfoFile = CHAMPINFOFILE(set);
			request.traitInfoFile = TRAITINFOFILE(set);
			request.compositionSize = compositionSize;
			request.gateType = settings[1] == 1 ? GateType::ActiveTraitTiers : GateType::ActiveTraits;
			request.emblemTraits = emblemTraits;
			request.connectedChampsOnly = settings[2] != 0;
			request.gateTimeoutSeconds = options.gateTimeoutSeconds;
			request.useCache = options.useCache;
			request.refreshCache = options.refreshCache;
			GatedCompositionResult result = getOrCalculateGatedCompositions(request);
			compList = move(result.compositions);
			cout << "Resolved from " << compositionResolutionName(result.resolution) << "." << endl;
		}
		else {
			compList = TeamComposition::generateComps(compositionSize, settings, seedComp);
			cout << "Generated without gates; disk cache was not used." << endl;
		}

		//Print compList
		vector<TeamComposition> listToPrint = compList;
		
		if (set == "12") set12TahmFilter(listToPrint);

		cout << endl << "Generated " << listToPrint.size() << " team compositions of size " << compositionSize << ": " << endl;
		count = 1;
		for (const TeamComposition& comp : listToPrint) {
			cout << setw(2) << count << ":" << comp.toString() << " | Num traits: " << comp.getActiveTraitsTotal() << endl;
			count++;
		}
		cout << "Total comps: " << listToPrint.size() << endl;

		double elapsedSeconds = chrono::duration<double>(chrono::steady_clock::now() - programStartTime).count();
		cout << endl << "Program runtime: " << elapsedSeconds << " seconds." << endl;
		return 0;
	}
	catch (const exception& ex) {
		cerr << endl << "Runtime error: " << ex.what() << endl;
		return 1;
	}
}
