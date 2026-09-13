# M1 / NET-01: legacy straight-road adapter

Implementation started 2026-09-13. This is the first M1 migration slice; it does not close the milestone. M0 measurement gaps remain open by explicit decision to proceed to M1.

## Units and identity

One existing world unit is defined as one meter for the compatibility graph. A tile is 16 meters, vehicle speeds retain their existing world-units-per-second values, and ground-level road centerlines have height zero. This establishes a consistent contract without retuning gameplay. It is not a claim that existing building proportions, walking catchments or simulation aging are physically calibrated.

`RoadNetwork` is an immutable derived snapshot. Each drivable tile center, including isolated roads, becomes a node; medians are excluded. Each physical adjacent connection becomes one straight segment. Its endpoint road classes are retained. Length is 16 meters cardinally or 16√2 diagonally. Segment direction permissions come from the accepted `World::canTravel` rules, including regional highway and roundabout rules. Directed lane count is the minimum endpoint lanes per direction, with the minimum endpoint speed. Lane records represent connection capacity; lane-changing and explicit junction movements are still future work.

Legacy node IDs are the tile index plus one. Segment IDs encode the lower endpoint tile and direction; lane IDs encode the segment, orientation and lane index. Zero is invalid. These deterministic **compatibility IDs** survive snapshot rebuilds, unrelated edits and save/reload. Demolishing and rebuilding at the same location reuses them: they must not be used as persistent ownership IDs. Future constructed geometry needs a separate monotonic ID namespace and saved allocation counters so demolition cannot alias an old owned reference. Do not silently convert these compatibility IDs into authoritative agent references.

The outgoing-permission array is the initial spatial lookup: constant-bounded neighbor lookup by tile, with node/segment/lane vectors for geometry and diagnostics. Snapshot construction currently scans the fixed map. Corridor coalescing and general spatial geometry indexes are deferred until measured and until parcel entrances can refer to points within a segment.

## Integration and migration safety

The bounded network worker constructs the adapter from its immutable `World` snapshot. Directed route searches use adapter permissions. Existing undirected straight-corridor acceleration, congestion costs, live lane movement, topology cancellation and snapshot publication remain in place. Added graph allocations are included in traffic memory estimates.

This is a derived-data migration: city v7 and historical loaders still own saved roads, trips, balances and cargo. On load, the normal worker rebuild derives the graph from accepted road data. There is no save-format change or rewriting of owned trip IDs. Existing city/traffic/rail tests exercise this integration, while the new `RoadNetworkTests` compares travel permissions for every neighboring pair across a map with straight roads, diagonals, one-way rules, a roundabout and regional highways. It checks unique references, immutable snapshot behavior and compatibility ID preservation through road serialization.

The graph does not yet replace renderer geometry or all tile-based routing data. A future elevation slice must introduce explicitly connected endpoints at matching heights, piecewise-linear approach ramps and clearance occupancy. Merely overlapping x/z coordinates must never connect different elevations. Rendering, price, occupancy and route acceptance must consume one validated construction result, published transactionally. Old flat roads must keep the current appearance and reachability.

## Remaining M1 gates

Validation: Debug and Release builds passed, followed by 10/10 CTest passes in each configuration, including the new adapter test and existing city/traffic/rail/save integration checks. Logs: `artifacts/net-01-release-tests.txt` (91.70 seconds) and `artifacts/net-01-debug-tests.txt` (223.84 seconds). Suites ran concurrently; durations are correctness evidence only. `git diff --check` passed.

Fresh measurement: three sequential Release runs of `scripts/benchmark-city.ps1 -Population 10000`, after builds completed, sustained 431.123–439.960 ticks per wall second with tick p95 4.001–4.065 ms and the 10k target met. Reports and source hashes are in `artifacts/perf-01-20260913-143714-744/`. These short seeded, blocking-worker runs do not establish metropolitan scale or isolate an adapter speedup from other machine conditions.

- SAVE-01 historical v1–v7 fixtures and full migration/conservation assertions.
- Flat-map resource fields, stable content IDs and versioned save sections preserving saved tuning.
- WORLD-01 ramped overpass through construction costs, occupancy, routing, voxel geometry and save/reload.
- Rejection tests proving failed construction leaves treasury and occupancy unchanged.
- M1 architecture review and updated remaining estimates before expanding M2–M5.

The current implementation provides no new elevated road, terrain, UI or building content. It establishes and exercises the migration seam those later systems need.
