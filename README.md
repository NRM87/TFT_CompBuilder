# TFT Comp Builder

TFT Comp Builder finds Teamfight Tactics compositions with strong trait coverage. It incrementally builds compositions, uses calculated gates to prune unpromising candidates, and caches both gate rows and completed composition lists on disk.

## Requirements

- Windows 10 or 11
- Visual Studio 2022 with the **Desktop development with C++** workload
- MSVC v143 and a Windows 10 or 11 SDK

The project uses C++20. No external packages need to be downloaded; the JSON library is included in the repository.

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
      --interactive        Use the prompt-driven setup flow
  -i, --input <path>       Read interactive answers from a file; implies --interactive
  -h, --help               Show command help
```

Trait arguments must match the names in the set's `TraitInfo.txt`. Names containing spaces are normally represented with underscores, such as `Space_Groove`.

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
  gates\
  compositions\
```

`Cache` is ignored by Git. Cache identities account for the set, champion and trait file contents, gate type, emblem multiset, connected-only mode, gate timeout, build profile, algorithm version, team size, and exact gate row. Editing `ChampionInfo.txt` or `TraitInfo.txt` therefore invalidates affected entries automatically.

Use `--refresh` to replace the matching cached calculation, or `--no-cache` for a run that neither reads nor writes cached data.

## Set data

Each supported set has a directory such as:

```text
TFT_CompBuilder\SetInfos\Set18\
  ChampionInfo.txt
  TraitInfo.txt
```

The default set is the highest numeric directory containing both files. Adding a later `Set<number>` directory therefore updates the default without recompiling the program.

Champion entries support weighted traits, wide champions, and interchangeable trait choices. See existing set files and the parser comments in `CompBuilderUtils.h` for the exact syntax.

## Troubleshooting

### Could not open a champion, trait, or set-data file

The executable is probably running with the wrong working directory. Change to the inner `TFT_CompBuilder` directory before launching it.

### The first run is slow

This is expected for an uncached size-9 request. Use a Release x64 build and allow the first run to populate `Cache`. Use a smaller `--size` while checking configuration or set data.

### Visual Studio starts the program in the wrong directory

Open the project properties and set **Debugging > Working Directory** to `$(ProjectDir)`, or run the executable from PowerShell as shown above.

### Cache results should be recalculated

Run with `--refresh`. Changes to champion or trait files are detected automatically, but `--refresh` is useful when deliberately retuning gates with the same inputs or timeout.
