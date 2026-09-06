# Traffic validation — 2026-09-05

Release and Debug builds pass both WorldTests and TrafficTests. The traffic integration suite passes with zero DirectX errors, including live road editing and window resizing. Close-up and whole-map captures were inspected for lane placement, orientation, colors, UI layout, and instanced rendering. The original road/editor suite is run separately with `--cars 0`.

## Measured performance

Machine: AMD Ryzen 9 5900X (12 cores / 24 threads), NVIDIA GeForce RTX 4070. Simulation is single-threaded. Rendered performance runs use Release, 2560×1440, VSync off, world ray-traced shadows enabled, seed 42, and exactly one 30 Hz simulation tick per rendered frame. This deliberately exercises simulation on every frame. The first 30 of 630 frames are excluded. Car shadows are disabled by design.

| Scenario | Actual active range | Frame p95 ms | Simulation p95 ms | GPU mean ms | Traffic storage MiB |
|---|---:|---:|---:|---:|---:|
| traffic-50k-close | 49,996-50,000 | 12.678 | 11.262 | 0.923 | 160.4 |
| traffic-50k-overview | 49,996-50,000 | 9.172 | 8.044 | 1.651 | 160.4 |
| traffic-100k-overview | 99,997-100,000 | 19.057 | 17.183 | 3.958 | 319.9 |
| traffic-editing | 4,998-5,000 | 12.981 | 4.139 | 0.931 | 39.3 |

The 50,000-car whole-map run meets the 16.67 ms total-frame target. The tighter 5 ms simulation p95 target was **not met**; the measured value above includes routing and traffic decisions as well as AVX2 integration. The updated 100,000-car run measured 19.057 ms frame p95, above 16.67 ms. Results depend on road layout, congestion, edits, CPU, and system load; the conflict-aware simulation does more work than the original whole-tile lock.

The large runs use a fully connected painted map to provide enough insertion space. `min_cars` can be several below target because arrivals are removed after that tick's spawning; the next tick replenishes them. No offscreen simulation culling is used. Whole-map captures show all active instances visible. Traffic storage is the sum of allocated CPU buffer capacities, excluding allocator metadata and process overhead; it is not process RSS or GPU memory.

The 50k render cases use 1,000 warmup ticks; the 100k case uses 2,400. Warmup is excluded from frame measurements and reported as `traffic_warmup_seconds`. The editing case uses 5,000 cars on the sparse network, 600 frames, continuous road changes, resize/minimize/restore, and the DirectX debug layer; it is not a 50k edit-time guarantee.

Headless benchmarks first wait for the exact target population and then measure 600 ticks:

```text
cars=50000 min=49999 ramp_ticks=972 ramp_seconds=6.38192 p50_ms=4.2031 p95_ms=6.308 completed=86 memory_mib=155.708
cars=100000 min=99999 ramp_ticks=2216 ramp_seconds=21.3359 p50_ms=7.7415 p95_ms=10.655 completed=124 memory_mib=314.815
```

## Intersection and chicane regression fix

The previous whole-tile reservation could deadlock two cars in adjacent bends even when their lanes were separate. A two-car chicane with seed 2 reproduced this: cars at (12,11) and (12,12) remained stopped for 1,800 ticks. Movement-level reservations let both trips finish. Another priority flaw allowed an older follower to win access ahead of a newly spawned lane leader; only the front car now requests access.

Added 384 seeded regression cases: 64 chicane runs spanning four rotations, 128 crossroads runs, 128 late-spawn runs (with explicit coverage of an older follower behind a newer leader), and 64 adjacent-junction runs. These require traffic to drain and check oriented car-body separation on every tick. Reservation validation also checks for conflicting or orphaned claims. AVX2 movement and the aligned SoA layout remain intact; conflict masks are cached and runtime arbitration is local to each tile. Four owner slots per tile and compact candidate lists avoid allocating a full owner/candidate grid for all 16 movements.

## Correctness coverage

- AVX2 versus an independent scalar oracle, including zero cars, partial eight-car groups, and untouched padding.
- Empty and isolated roads, disconnected components, straight lanes, corners, junctions, dead-end U-turns, safe population limits, trip completion, replenishment, and target reduction.
- Lane following gaps both inside tiles and across their boundaries; finite positions, bounded speeds, and stable-ID/dense-slot mapping.
- Single-car continuous movement without jumps, deterministic fixed-seed reproduction, bounded catch-up, paused traffic, and invalid negative time deltas.
- Road deletion, network splits, reachable trip repair, preservation of unaffected identities/positions, failed-load preservation, and successful-load reset.
- Search continuation with a one-expansion-per-tick budget. The 100k benchmark exercises geometric buffer growth beyond 65,536 slots and validates the final state.

## Behavior and limits

Junction reservations use a precomputed 16-movement conflict table based on swept car footprints. Non-conflicting movements can share a tile; crossing paths and merging exits remain mutually exclusive. Only lane-front cars participate in first-arrival arbitration, and a departing car retains its previous reservation until its rotating rear has cleared. Entry requires downstream clearance unless the trip ends inside that tile. Routing accounts for incoming direction so repairs can reach a dead end, turn around legally, and return. Congestion can still gridlock; cars are not deleted merely to clear queues. If a road edit changes the current exit, the car waits for exclusive use of its tile and follows a temporary connector from its actual position to the repaired route.

Road-mask updates are local, but connected components and corridor links rebuild atomically on the main thread after a topology revision. Compressed route validation uses row/column prefix sums. Large or frequent edits can therefore cost more than steady-state motion. Random spawning is bounded by both routing work and available physical space. Cars remain transient; map files keep their existing format.

## Reproduce

```powershell
.\scripts\build.ps1 -Configuration Debug
.\scripts\build.ps1 -Configuration Release
.\scripts\verify-traffic.ps1
.\scripts\verify.ps1
```

JSON, BMP, and headless timing artifacts are written under `artifacts/traffic-*`. Timing results vary with system load.
