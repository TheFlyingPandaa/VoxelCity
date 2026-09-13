# BASE-01: source and test baseline

Recorded 2026-09-13. This completes the documentation/source-baseline task from the [parity backlog](CITIES_SKYLINES_2_PARITY_PLAN.md); it does not close M0 or establish metropolitan parity.

## Source identity

Base commit: `7412785bea1ee02ba4760e51b7ab81aa4e4b3946`. The tested source includes existing uncommitted density, roads, railway, vegetation, rendering and test changes. The commit alone cannot reproduce this working tree. BASE-01 changes documentation and adds a source-capture helper; gameplay source is preserved.

The local snapshot is `artifacts/base-01-source/source.zip`, accompanied by `manifest.json` (revision, Git status and SHA-256 for each archived file) and `source.sha256.txt`. It contains tracked and non-ignored untracked files, including the current docs and capture helper. Build outputs, artifacts, ignored saves and Git history are excluded. This is a source snapshot, not the historical save-fixture collection required by SAVE-01. Artifact files are local and ignored by Git; retain the snapshot and logs together when sharing this baseline.

To capture another snapshot from an idle working tree:

```powershell
.\scripts\capture-source-baseline.ps1
```

The helper creates a timestamped directory and refuses to overwrite an existing one. To reproduce this source, extract the ZIP into a separate directory, verify extracted file hashes against the manifest and build there. Configuration still needs the pinned Dear ImGui dependency; the archive is not an offline dependency bundle.

## Verified feature contracts

| Contract | Current implementation / source |
|---|---|
| City save | v7 writer; v3-v6 city loaders and v1/v2 road-map imports in `src/City.cpp` |
| Traffic payload | v4 writer; v1-v4 reader in `src/Traffic.cpp`; unfinished logical requests restart |
| Density | Low/high residential and commercial, shared demand/taxes, two development levels; [details](DENSITY_AND_ROADS.md) |
| Roads | Eight travel directions, road classes, wider footprints, fixed roundabouts/interchanges; arbitrary curves and elevations remain roadmap work |
| Rail | Regional passenger/freight, automatic local shuttles/transfers, stations, terminals, depots, crossings and a fixed highway overpass; [details](RAILWAY.md) |
| Population | Aggregate households and sampled owned trips, not persistent individual lifepaths |
| Scheduling | Main-thread simulation with immutable worker snapshots; controlled headless drains support reproducibility, ordinary async timing may differ |

README, MVP status and the density guide now agree on these contracts. Historical performance and soak results remain labelled as historical; they are not evidence from this run.

## Fresh build and test evidence

Environment: Windows x64, Visual Studio 2022 generator, MSBuild 17.14.40, CMake 4.3.1 and Windows SDK 10.0.26100.0 selected by configuration. CPU registry reports AMD Ryzen 9 5900X. Fresh GPU/RAM inventory queries were unavailable; the older RTX 4070 reference-machine description is historical. No frame-rate or memory benchmark was run here.

| Check | Result | Local log |
|---|---|---|
| Debug configure and serial build | Passed | `artifacts/base-01-debug.txt`, `artifacts/base-01-debug-serial.txt` |
| Debug CTest | 9/9 passed, 164.85 seconds total | `artifacts/base-01-debug-tests.txt` |
| Release configure and serial build | Passed | `artifacts/base-01-release-host.txt`, `artifacts/base-01-release-host-serial.txt` |
| Release CTest | 9/9 passed, 58.23 seconds total | `artifacts/base-01-release-tests.txt` |

Both configurations register `WorldTests`, `TrafficTests`, `CityTests`, `VegetationTests`, `HighwayTests`, `VoxelTests`, `VoxelGpuTests`, `ThreadingTests` and `RailwayTests` in `CMakeLists.txt`. GPU testing checks voxel traversal against the CPU; it is not a rendered city soak or a performance claim.

Initial build-helper attempts failed during parallel MSBuild without compiler diagnostics. A serial diagnostic attempt succeeded for Debug. Release in the restricted execution environment reported MSB6001 with duplicate `PATH`/`Path` environment entries; clearing them in the child shell did not resolve it. A serial Release build outside that environment succeeded. No project build setting was changed. The build-helper logs above include failed builds after successful configuration; use the separate serial logs for build outcomes.

Successful command shape (use the CMake installation selected by `scripts/build.ps1`):

```powershell
cmake --preset windows
cmake --build --preset debug --parallel 1 -- /nr:false
ctest --preset debug
cmake --build --preset release --parallel 1 -- /nr:false
ctest --preset release --timeout 180
```

This run used `F:/VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe` and its sibling `ctest.exe`.

## Outstanding gates

- City, railway, traffic, threading and renderer integration scripts, growth benchmarks, packaging and the two-hour real-time rendered soak were not rerun for BASE-01.
- Full historical v1-v7 fixture collection and migration/conservation coverage remain SAVE-01 work; current loader support does not prove every historical scenario.
- Subsystem metrics, representative higher-population fixtures and fresh CPU/GPU/RAM performance evidence remain PERF-01/M0 work.
- The new-player study remains in the separate interface rework. This baseline makes no usability claim.
