# TFT Comp Builder

TFT Comp Builder finds Teamfight Tactics compositions with strong trait coverage. It incrementally builds compositions, uses calculated gates to prune unpromising candidates, and caches both gate rows and completed composition lists on disk.

## Requirements

- Windows 10 or 11
- Visual Studio 2022 with the **Desktop development with C++** workload
- MSVC v143 and a Windows 10 or 11 SDK

The project uses C++20. No external packages need to be downloaded; the JSON and local HTTP server libraries are included in the repository.

## Build with Visual Studio

1. Open `TFT_CompBuilder.sln` in Visual Studio 2022.
2. Select **Release** and **x64** from the solution configuration controls.
3. Choose **Build > Build Solution**, or press `Ctrl+Shift+B`.

The executable will be created at:

```text
x64\Release\TFT_CompBuilder.exe
```

Release builds are strongly recommended. Gate calculation uses elapsed time to tune pruning, so a Debug build will calculate different, usually more aggressive gates and run considerably slower. Debug and Release caches are kept logically separate.

## Build from the command line

Open **Developer PowerShell for VS 2022**, change to the repository root, and run:

```powershell
msbuild .\TFT_CompBuilder.sln /m /p:Configuration=Release /p:Platform=x64
```

If `msbuild` is not recognized, use the Developer PowerShell shortcut installed with Visual Studio rather than a regular PowerShell window.

## Run

The program reads `SetInfos` and writes `Cache` relative to its working directory. Run it from the inner `TFT_CompBuilder` project directory:

```powershell
cd .\TFT_CompBuilder
..\x64\Release\TFT_CompBuilder.exe
```

For the local browser interface, run:

```powershell
..\x64\Release\TFT_CompBuilder.exe --serve
```

The program binds only to `127.0.0.1`, serves the frontend from `Web`, and opens `http://127.0.0.1:8765/` in the default browser. Press `Ctrl+C` in the terminal to stop it. Use `--no-open` to leave the browser closed or `--port` to select another local port.

With no options, the program:

- Finds the latest numeric `SetInfos\Set<number>` directory, currently Set 18.
- Builds size-9 compositions.
- Uses active-trait gates rather than total trait-tier gates.
- Uses no emblems and considers all champions.
- Gives each gate probe a 10-second timeout.
- Reads and writes the disk cache.

The first request for a configuration can take a while because its gates and compositions must be calculated. Later identical requests load the cached composition list. When only the requested composition size is new, the program reuses compatible cached gates and calculates the missing size row.

## Command-line options

```text
Usage: TFT_CompBuilder.exe [options]

  -s, --set <number>       TFT set to load (default: latest available set)
      --size <1-10>        Team composition size (default: 9)
      --gate-type <type>   traits (default) or tiers
      --emblem <trait>     Add an emblem; repeat for multiple or duplicate emblems
      --connected-only     Only extend comps with connected champions
      --gate-timeout <s>   Gate probe timeout in seconds (default: 10)
      --refresh            Ignore matching caches and recalculate gates
      --no-cache           Do not read or write the disk cache
      --cache-info         Show cache usage and exit
      --cache-prune        Remove stale, invalid, and orphaned cache data, then exit
      --cache-max-mb <n>   With --cache-prune, evict least-recently-used entries to this limit
      --serve              Start the local web interface and open it in a browser
      --port <1-65535>     Port for --serve (default: 8765)
      --no-open            With --serve, do not open the browser automatically
      --interactive        Use the prompt-driven setup flow
  -i, --input <path>       Read interactive answers from a file; implies --interactive
  -h, --help               Show command help
```

Trait arguments must match the names in the set's `TraitInfo.txt`. Names containing spaces are normally represented with underscores, such as `Space_Groove`.

## Local web interface

The browser interface provides the normal set, size, gate objective, emblem, connected-only, timeout, refresh, and cache options. Calculations run asynchronously, so the local server remains responsive while the browser polls job status. Results are paged in groups of 100 and can be filtered by champion on the current page. Cache inspection and conservative pruning are available in the same interface.

The current set metadata is still initialized globally by the calculation engine, so the local server deliberately runs one calculation job at a time. Gate tables are no longer global mutable state: cached or newly calculated gates are passed explicitly into composition generation. This makes the gate path safe to reuse from other frontends and removes reliance on process-static gate values.

The local API currently exposes:

```text
GET  /api/sets
POST /api/jobs
GET  /api/jobs/{id}?offset=0&limit=100
GET  /api/cache
POST /api/cache/prune
```

This request/job boundary is intended to remain stable if the frontend is later hosted separately. A public multi-user deployment should isolate calculations in worker processes or first make the remaining set metadata instance-owned.

## Examples

Run the default latest-set, size-9 active-trait search:

```powershell
..\x64\Release\TFT_CompBuilder.exe
```

Find size-8 compositions for Set 18:

```powershell
..\x64\Release\TFT_CompBuilder.exe --set 18 --size 8
```

Optimize for total active trait tiers instead of unique active traits:

```powershell
..\x64\Release\TFT_CompBuilder.exe --gate-type tiers
```

Add several emblems:

```powershell
..\x64\Release\TFT_CompBuilder.exe --emblem Fae --emblem Primal
```

Duplicate emblems are supported and remain distinct in the cache identity:

```powershell
..\x64\Release\TFT_CompBuilder.exe --emblem Fae --emblem Fae
```

Force fresh gate and composition calculation:

```powershell
..\x64\Release\TFT_CompBuilder.exe --refresh
```

Inspect the disk cache without running a composition search:

```powershell
..\x64\Release\TFT_CompBuilder.exe --cache-info
```

Remove incomplete writes, invalid manifests, orphaned objects, and legacy v1 cache files:

```powershell
..\x64\Release\TFT_CompBuilder.exe --cache-prune
```

Do the same cleanup and then evict the least-recently-used entries until the cache is at most 512 MiB:

```powershell
..\x64\Release\TFT_CompBuilder.exe --cache-prune --cache-max-mb 512
```

Use the original prompt-driven workflow:

```powershell
..\x64\Release\TFT_CompBuilder.exe --interactive
```

Read prompt answers from a file:

```powershell
..\x64\Release\TFT_CompBuilder.exe --input SourceInput.txt
```

## Cache behavior

Generated files are stored beneath:

```text
TFT_CompBuilder\Cache\
  v2\
    manifests\
      gates\
      compositions\
    objects\
      gates\
      compositions\
    staging\
```

`Cache` is ignored by Git. Small request manifests point to immutable, SHA-256-addressed gate and composition objects. Composition objects use a champion-label dictionary to avoid repeating full names in every row. An exact composition hit only needs its composition manifest and object; it does not need the corresponding gate cache to still exist.

Cache identities account for the set, champion and trait file contents, gate type, emblem multiset, connected-only mode, gate timeout, build profile, executable fingerprint, and team size. Editing `ChampionInfo.txt` or `TraitInfo.txt`, changing a cache-relevant option, or rebuilding a changed executable therefore selects a fresh cache entry automatically. Writes are serialized between processes and published atomically. A cache read, validation, or write failure is treated as a cache miss so it cannot prevent a calculation from completing.

Use `--refresh` to replace the matching cached calculation, or `--no-cache` for a run that neither reads nor writes cached data.

`--cache-prune` is conservative unless a size limit is supplied: it removes temporary files, invalid manifests, orphaned immutable objects, and old v1 `gates`/`compositions` directories. Adding `--cache-max-mb` also evicts manifests in least-recently-used order and removes objects that become unreferenced. Existing v1 files are not migrated because they do not contain the stronger v2 identity and integrity metadata.

## Set data

Each supported set has a directory such as:

```text
TFT_CompBuilder\SetInfos\Set18\
  ChampionInfo.txt
  TraitInfo.txt
```

The default set is the highest numeric directory containing both files. Adding a later `Set<number>` directory therefore updates the default without recompiling the program.

Champion entries support weighted traits, wide champions, and interchangeable trait choices. See existing set files and the parser comments in `CompBuilderUtils.h` for the exact syntax.
