# PERF-01: first M0 measurement slice

Recorded 2026-09-13. M0 is in progress. Subsequent work has started the [M1 road adapter](ROAD_NETWORK.md) at the user's request; M2–M5 have not started. This report covers the first instrumentation slice of the parity backlog, not the complete PERF-01 acceptance gate.

`CitySimulation::performance()` accumulates wall-clock milliseconds for access work in ticks/steps, rail and rail events, traffic, road trip events, service aggregation, employment/demand, development, dispatch, finance and publication. Counts distinguish ticks, completed city steps and deferred city steps. Totals are exclusive between these measured phases, not a complete profiler of construction, refresh or worker execution. The existing `tickMs` remains the last completed city step. Counters never influence gameplay or enter save files; `resetPerformance()` starts a measurement window without changing simulation state.

Rendered benchmark JSON now includes these totals and their tick/step counts for the city object's lifetime, including warmup. They must not be compared directly with frame samples that exclude warmup. `route_batch_age_ms` measures the currently submitted batch from submission through consumption, including worker queue and execution time. It excludes requests waiting for submission and is zero when no batch is outstanding; full route-request queue age remains outstanding. Blocking worker mode normally consumes batches before sampling.

Build Release, then run:

```powershell
.\scripts\benchmark-city.ps1
# Or one fixture: population, measured ticks, warmup ticks
.\build\Release\CityBenchmark.exe 10000 900 300
```

The script records three sequential runs per requested population, source hashes, revision, working-tree status and CPU identity. JSON reports include measured tick p50/p95/p99, achieved ticks per wall second, simulation seconds, working set, active traffic and population attainment. Exit code 2 from the executable means the population target was missed; the wrapper retains all reports and fails after finishing the matrix. Invariant failures return 1. Timing excludes fixture creation, warmup and final invariant validation. Completed/failed trip counts are lifetime counters, not window deltas. These are populated, seeded aggregate scenarios with ordinary owned trips, not organically grown individual populations or injected vehicle stress.

## Fresh measurements

Release, AMD Ryzen 9 5900X, seed 42, blocking worker, 300 warmup ticks and 900 measured ticks (30 simulation seconds), three runs after builds completed. Local reports: `artifacts/perf-01-20260913-140538-941/`, including `environment.json`. Earlier exploratory runs during builds are superseded.

| Requested residents | Minimum actual residents | Ticks/wall second range | Tick p95 range | Outcome |
|---|---|---|---|---|
| 10,000 | 10,000 | 274.857–279.621 | 6.634–6.726 ms | Fixture target met |
| 25,000 | 18,176 | 247.331–249.739 | 6.807–6.925 ms | Target failed |

The current metropolis scenario has a fixed footprint and caps seeded population at 18,176. This is a fixture limitation, not a demonstrated simulation capacity limit. Do not label the second row a 25k benchmark. Traffic dominates the measured cost in this short scenario. These headless results make no GPU, rendering, 100k, long-session or financial-solvency claim.

## Remaining work

Validation: full Debug and Release builds passed. CTest passed 9/9 in both configurations (Release 88.60 seconds, Debug 226.09 seconds); logs are `artifacts/perf-01-release-tests.txt` and `artifacts/perf-01-debug-tests.txt`. The suites ran concurrently, so their durations are correctness evidence, not comparable performance measurements. The new city regression checks tick/step accounting and byte-identical saves across diagnostic reset. Invalid benchmark duration is rejected. `git diff --check` passed. Builds required the same unrestricted MSBuild environment described in BASELINE.md because the restricted environment reproduced the duplicate PATH/Path failure.

- Build representative higher-population fixtures and longer activity windows.
- Measure full request queue age, construction command latency, requested 2×/3× versus achieved speed, and fresh hardware/RAM/VRAM context.
- Run the outstanding city/rail/traffic/threading/renderer integration scripts and growth benchmark from BASELINE.md.
- Collect SAVE-01 historical fixtures before NET-01/WORLD-01 and the M1 architecture gate.
- Keep the full M2–M5 capabilities behind the ordered foundation and migration work in the parity plan.
