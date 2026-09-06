# City MVP implementation status

## Implemented

| Area | Behavior |
|---|---|
| City state | Headless fixed-tick simulation, stable IDs, households, workplaces, seeded growth and persistent trip ownership |
| Construction | Priced atomic road strokes, RCI zoning, one-tile facilities, demolition, occupancy checks, one-way rules and signals |
| Development | Migration, jobs, RCI demand, two building levels, service/land-value requirements, deterioration, abandonment and recovery |
| Economy | RCI tax controls, periodic financial ledger, upkeep, trade payments, one emergency loan, insolvency handling and sandbox |
| Utilities | Electricity, water and sewage capacity per connected road component, with proportional shortages |
| Services | Garbage, clinic and fire dispatch; road-distance police, school and park coverage; bounded waste, health, fire, crime and education |
| Environment | Local industry/waste/power pollution, local traffic pressure/noise, happiness and land value |
| Traffic | Sampled household commutes, inventory-carrying freight, service trips, endpoint callbacks, congestion-weighted routing and commuting productivity |
| Interface | Construction categories, city overview, budget/demand controls, service and condition overlays, inspection, progression guide and time controls |
| Persistence | Checked v4 city saves, v3 city compatibility, v1/v2 imports, complete active routing/vehicle state, paused-edit reconciliation, atomic file replacement, rotating autosaves and exit protection |
| Rendering | Instanced procedural buildings, shared BLAS, distance-based window detail, building shadows, GPU overlays and asynchronous large road rebuilds |
| Delivery | Starter/town/metropolis fixtures, automated city input tests, growth and stress runners, local Windows package script |

The agreed scope remains CS1's core management loop for a small-team voxel game. It does not claim original-game feature breadth or production content polish. Public transport, curved/elevated roads, terrain editing, districts, high-density zoning, specialist industries and individual citizen lifecycles remain outside this release.

## Regional highway addition

New city maps and scenarios now include a protected east-west highway between Westhaven and Eastbridge, with opposing carriageways, shoulders, a grass median and a flat central access junction. It carries seeded through traffic immediately and replenishes traffic at the appropriate outside entrances. Imports and exports choose valid inbound and outbound endpoints. Existing city saves keep their layout; v3 remains loadable and new saves use v4 without changing the traffic payload format.

The new-map highway capture was visually inspected (`artifacts/highway-default.bmp`) and the rendered run reported zero DirectX errors. Debug and Release pass all four test executables; the city UI and 1�/3� stress scripts also pass.

`HighwayTests` covers regional ownership, atomic rejection of highway demolition, shoulder access restrictions, local-road frontage, highway speeds, all four neighboring-city trade directions, gameplay imports, deterministic active-trip save/resume and legacy v3 loading.

## Acceptance evidence

Validated locally on Windows with a Ryzen 9 5900X and RTX 4070.

- Debug and Release builds run `WorldTests`, `TrafficTests` and `CityTests`.
- City tests cover priced command atomicity, occupancy and unlock checks, demand/growth, utility cuts and recovery, saved-state determinism, corruption rollback, legacy imports, duplicate trip rejection, directed travel, road-edit failures, pause/speed behavior and bypass travel time.
- A paused-edit save regression deletes a road occupied by a vehicle, saves without advancing time, reloads and validates the resulting traffic state.
- `CityTests growth` builds roads, zones and facilities with normal priced commands from $100,000. The city reaches **10,000 residents**, then maintains positive income at that population for **30 consecutive financial periods**. Result: `artifacts/city-growth.txt`.
- The accelerated two-hour simulation soak passed with periodic road edits and 23 rotating save/reload cycles: **10,000 residents**, 5,998 completed goods deliveries, 432 active vehicles, 19 pending trips and positive operating income at completion. It ran in 193.291 seconds locally. Result: `artifacts/city-soak.txt`.
- The packaged executable passed a town smoke test from its own directory with bundled shaders, data and runtime DLLs and zero DirectX errors (`artifacts/package-smoke.json`).
- The city mouse integration test selects construction tools through ImGui, paints a road stroke and residential parcel, bulldozes it and places a power facility, checking spending and UI capture.
- The legacy renderer/input and traffic integration scripts completed successfully with zero reported DirectX errors. Their historical 50k/100k vehicle performance is not a city release guarantee.

Representative 1440p Release results after the regional highway addition (630 rendered frames, first 30 omitted, shadows on):

| Scenario | Residents | Minimum active vehicles | Frame p95 |
|---|---:|---:|---:|
| Metropolis with DirectX validation | 10,000 | 472 | 4.024 ms |
| City plus traffic stress, 1× | 10,000 | 5,079 | 4.082 ms |
| City plus traffic stress, 3× | 10,000 | 5,207 | 12.048 ms |

Stress trips run in an additional grid adjoining the city. The runner waits for the actual vehicle target before measurement. These results exceed the 1× 16.67 ms and 3× 33.3 ms frame targets on this machine; they do not establish minimum hardware requirements. The benchmark executes one or three fixed ticks per frame, respectively. See `artifacts/city-*.json` and inspected BMP captures.

## Remaining release gates

- The five-person usability gate requires actual new players; it cannot be established by automated mouse tests.
- A two-hour **real-time rendered** soak with repeated saves/edits and process-memory tracking remains required before declaring the release fully validated.
- `CityTests soak` exercises two hours of simulated time with periodic road edits and rotating save/reload. Its accelerated result is recorded separately in `artifacts/city-soak.txt`; it is not a substitute for the real-time rendered gate.

## Intentional simplifications

Households provide grouped population and workforce, with sampled commutes and a single goods commodity. Municipal import/export transactions occur at delivery and are separate from the recurring operating balance. Coverage services use road distance; dispatched facilities have four concurrent vehicles. Road-component utility distribution does not require pipes, wires or fluid simulation. New buildings use procedural geometry rather than a production asset library.

The existing ray-tracing hardware requirement is retained. Map startup may still perform synchronous initial meshing; large later changes use background batches, so updated road geometry can appear progressively. City rule evaluation remains single-threaded and deterministic at fixed ticks; sub-tick display interpolation resets on load.

## Reproduce

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\build.ps1 -Configuration Debug
.\scripts\verify-city.ps1 -Growth
.\build\Release\CityTests.exe soak
.\scripts\verify.ps1
.\scripts\verify-traffic.ps1
.\scripts\package.ps1 -SkipBuild
```
