# VoxelCity

A C++20 / DirectX 12 voxel city builder for Windows. Build a small city through road construction, residential/commercial/industrial zoning, jobs, utilities, taxes and essential services. The current release targets a **10,000-resident city** using household simulation and physical commute, freight and service vehicles.

This is an implementation of the Cities: Skylines **core-loop MVP**, with simplified systems. Low/high-density residential and commercial zoning and automatic passenger/freight rail are implemented. Manual transit lines, additional transport modes, terrain editing, general curved/elevated roads, districts and individual citizen lifecycles remain deferred. See the [parity roadmap](docs/CITIES_SKYLINES_2_PARITY_PLAN.md). See [implementation and validation status](docs/CITY_MVP.md).

## Build and play

Requirements: Windows 10/11 x64, AVX2 CPU, DXR 1.1 / Shader Model 6.5 GPU, Visual Studio 2022 C++ tools, Windows SDK 10.0.22621+, CMake 3.24+ and Git. The build helper locates the newer Visual Studio CMake when PATH contains an older version. Configuration downloads pinned Dear ImGui v1.91.9b.

```powershell
.\scripts\build.ps1 -Configuration Release -Run
```

The default game starts with an east-west regional highway connecting the neighboring cities of **Westhaven** and **Eastbridge**. Separate carriageways have shoulders, a grass median and a central access junction with a short local road. Regional traffic is present from the start; extend that local road to build your city. Choose **New / starter city** for a connected example layout, or launch:

```powershell
.\build\Release\VoxelCity.exe --city-scenario starter
.\build\Release\VoxelCity.exe --city-scenario town
.\build\Release\VoxelCity.exe --city-scenario metropolis
```

See [quick start](docs/CITY_QUICKSTART.md) for the gameplay loop, controls and save recovery.

Natural broadleaf and conifer groves populate undeveloped land. Roads, zoning and facilities automatically clear their footprints at no extra cost. Cleared trees stay gone after demolition and save/load. Older city saves gain trees on unused land; previous demolition history cannot be recovered. Diagnostic road scenarios remain tree-free.

## Simulation and persistence

- Flat 512 脳 512 road-tile map; each tile spans 16 脳 16 voxels over 2 脳 2 grid spaces. Roads support eight travel directions, road classes, wider footprints and fixed roundabout/interchange prefabs. See [density and roads](docs/DENSITY_AND_ROADS.md). One-way direction and signal tools operate on existing road tiles.
- Persistent households, workplaces and inventories generate owned trips. Only successful freight arrivals deliver goods. Failed domestic deliveries return goods to their source when it still exists. Imports are paid on arrival; exports earn revenue on arrival.
- Electricity, water and sewage capacity follow connected road components. Schools, police and parks have road-distance coverage and capacity. Garbage, clinic and fire facilities dispatch up to four concurrent vehicles each.
- Vehicles run at 30 Hz, city rules every simulated second and financial settlement every simulated minute. Pause and 1脳/2脳/3脳 controls apply to the whole city. Congestion delays deliveries and reduces commuting productivity; vehicles are not deleted merely to clear queues.
- Construction checks the entire road stroke for occupancy and affordability before spending. Service prices, capacities, upkeep and unlocks live in `data/buildings.csv`; saves preserve their tuning definitions.
- Version 7 `.vcity` saves contain roads/rules, city state, random states, households, inventory, active vehicles, owned-trip queues, density, permanent tree clearing and railway/train state. The embedded traffic payload is version 4; unfinished searches restart from saved logical requests. Candidate saves are validated before replacing the active city. Version 1/2 road maps import into empty cities with starting funds, and version 3-6 city saves remain readable. Existing saves retain their authored layout; the regional highway and railway are included in new maps and city scenarios.
- Atomic saves use a flushed temporary file and replacement. Three autosaves rotate every five real-time minutes while unpaused under `%LOCALAPPDATA%\VoxelCity\Autosaves`. Closing a modified city offers save/discard/cancel. Loading resets the sub-tick rendering accumulator; fixed-tick simulation state is preserved.

## Rendering

The default renderer traces actual voxels with DirectX 12 / DXR 1.1. Hardware traversal finds occupied bounding boxes; custom shader traversal resolves packed 8󭅌 voxel bricks. Roads use losslessly compressed homogeneous voxel columns, while quarter-unit building and vehicle models share geometry across instances. There is no dense allocation covering the 8,192�8,192 world.

Natural daylight combines soft sun shadows, occluded skylight, one diffuse bounce, material-dependent reflections, temporal accumulation, spatial filtering, HDR tone mapping and subtle distance haze. Buildings have differentiated roofs, windows, trim and service silhouettes. Cars cast shadows and appear in reflections, including when offscreen. Building windows reveal voxel room interiors with Fresnel reflections; vehicle glass remains opaque. Tree groves share four voxel models and appear in both renderers.

Choose **Low / Medium / High** lighting quality in the performance panel. High is the default; it renders primary visibility at native resolution and secondary lighting at half resolution. The build grid starts hidden and remains available in the build panel. The previous mesh renderer is available with `--renderer legacy`.

```powershell
.\build\Release\VoxelCity.exe --city-scenario town --close-up
.\build\Release\VoxelCity.exe --quality medium
.\build\Release\VoxelCity.exe --renderer legacy
.\scripts\verify-voxel.ps1
```

The rendering target is 1440p/60 on an RTX 4070 with a 10,000-resident city at 1� speed. This is an independent Teardown-inspired implementation using hardware acceleration, not Teardown's engine. See [renderer design and validation](docs/VOXEL_RENDERER.md) for measurements, quality limits and reproduction commands.

## Validation and packaging

```powershell
.\scripts\build.ps1 -Configuration Debug
.\scripts\verify-city.ps1
.\scripts\verify-city.ps1 -Growth
.\scripts\verify.ps1
.\scripts\verify-traffic.ps1
.\scripts\package.ps1 -SkipBuild
```

`WorldTests`, `TrafficTests`, `CityTests`, `VegetationTests`, `HighwayTests`, `VoxelTests`, `VoxelGpuTests`, `ThreadingTests` and `RailwayTests` run through the build helper (nine CTest targets). `VoxelGpuTests` requires a supported GPU and compares 8,192 shader rays against CPU traversal. `CityTests growth` constructs the test city using priced commands and waits for growth; `CityTests 1800` runs an existing metropolis for 30 financial periods. The optional growth check takes several minutes. `CityTests soak` runs two simulated hours with periodic road edits and rotating save/reload; this is an accelerated headless check, not a real-time rendered soak.

`--city-stress 5000 --traffic-benchmark` adds explicitly labelled diagnostic trips in a road grid adjoining the city, warms up until **actual active vehicles** reach the target, then measures the rendered scenario. It is a stress workload, not a claim that 10,000 residents normally generate 5,000 simultaneous cars. `--city-speed 3` exercises three simulation ticks per benchmark frame. Reports and captures go to `artifacts`.

Packaging creates a local ZIP with the executable, shaders, tuning data, notices, quick start and app-local VC143 runtime DLLs. It does not publish or upload anything.

## Road / traffic diagnostics

The earlier road sandbox remains available through `--scenario empty|network|full|showcase` or `--cars N`. Random trip spawning and the car-target slider belong to that diagnostic mode. Pure road saves there still use version 2 and do not contain city state.

```powershell
.\build\Release\VoxelCity.exe --scenario showcase --cars 100 --traffic-seed 42
.\build\Release\VoxelCity.exe --benchmark --scenario full --cars 50000 --traffic-benchmark --traffic-warmup 1000 --frames 630 --report artifacts/traffic.json
```

Voxel capture flags: `--camera-path`, `--no-ui`, `--no-grid`, and `--capture-sequence folder` (one BMP per frame after warmup; capture overhead invalidates performance measurements).

Other diagnostic flags: `--debug-layer`, `--overview`, `--close-up`, `--no-shadows`, `--frames N`, `--capture path.bmp`, `--report path.json`, `--edit-stress`, `--window-test`, `--interaction-test`, `--city-input-test`. Install Windows Graphics Tools for the DirectX debug layer. Output directories must exist. Automated runs return nonzero on failure; errors are logged to `voxelcity-error.log` / `d3d12-debug.log`.

Current source/test baseline: [BASE-01](docs/BASELINE.md). Historical baseline results: [world](docs/VALIDATION.md), [traffic](docs/TRAFFIC_VALIDATION.md). [Third-party notices](THIRD_PARTY.md).

Roundabout prefab: select it in the city build menu and click to place a 6 x 6 grid footprint (3 x 3 road tiles) for $160. Connect roads at the midpoint of each side. Circulation is counterclockwise; entry/exit paths and the central island are fixed. Right-click any part to remove the entire piece for $40. City saves preserve the prefab.

Road building: choose Two-lane road in the city build menu, click and drag to your destination, then release to place the previewed line. The same tool builds straight and diagonal streets in all eight travel directions. Press Esc to cancel a line. Diagonal shoulders reserve neighboring parcels. The 6 x 6 roundabout now uses a circular island, narrower circular lanes and tangent direction arrows.


## Threading

The main thread owns input, UI layout, city state and vehicle movement. A dedicated render thread owns DirectX, GPU uploads, presentation and captures. One shared pathfinding thread computes vehicle routes, road connectivity and service coverage from private snapshots. Gameplay applies completed results on later ticks; exact trip timing can vary with worker scheduling.

Road edits invalidate old network jobs. Vehicles wait safely while connectivity is rebuilt; coverage-dependent city decisions pause until the current coverage result arrives. Rendering uses copied UI commands and a bounded latest-frame mailbox. No live ImGui context or mutable simulation world is shared with the render thread.

Existing saves remain readable. Unfinished searches restart after loading without losing owned trips. New traffic payloads require this build or newer.

Run `.\scripts\verify-threading.ps1` for delayed-worker, input, resizing, capture and rendering checks. `ThreadingTests` also checks pending saves, cancellation, stale routes, coverage and queue bounds. Headless correctness tests explicitly drain worker jobs for reproducibility. Frame-limited diagnostic runs wait for each submitted frame so captures and measurements are reliable; ordinary gameplay runs asynchronously. Benchmark reports include main-thread work, presentation timing, render-worker time, route latency and queue diagnostics. See [thread ownership and validation](docs/THREADING.md).

### Railway and trains

New cities include a protected regional railway with passenger and freight trains. Open **Railway** in the build panel to add tracks, stations, cargo terminals, depots and highway overpasses. Services run automatically; depots enable local passenger shuttles. See [Railway guide](docs/RAILWAY.md) for construction, prices, cargo and save compatibility.
