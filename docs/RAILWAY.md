# Railway and trains

Every new city and starter scenario includes a protected double-track regional railway across row 192, parallel to the highway. Passenger and freight trains run immediately. Existing saved maps retain their original layout; railway tools work in those cities without inserting a regional line.

## Build and operate

Open **Railway** in the build panel. Drag a double-track stroke along one grid axis, starting on an existing tile to connect. Successive strokes create rounded turns and automatic switches. A preview shows the complete footprint, validation result and total price before placement.

Passenger stations, cargo terminals and depots occupy four building tiles beside four platform/siding tiles. Press **R** or **Rotate station** to rotate. All four platform tiles must be straight and unobstructed. Buildings need local-road frontage and power, water and sewage to operate. Platforms can be built on the protected regional railway. Removing a facility releases its entire footprint while retaining the running tracks.

Straight two-lane streets and ground-level railway can cross perpendicularly. Gates stop approaching cars; trains wait for cars already inside the crossing and keep the gates closed until the final carriage clears. At a shared crossing, right-click with the road tool to remove the road, or the railway/bulldozer tool to remove local rail.

The **Highway rail overpass** fits across a straight divided highway. Place its center in the median, then connect both ramp ends. The ramps and supports reserve their corridor. Highway traffic continues below the deck. Bulldozing any overpass tile removes the entire overpass and preserves the highway.

## Automatic services

- Regional passenger and freight services connect operational facilities to a reachable map-edge rail tile, without requiring a depot.
- A reachable depot enables local passenger shuttles. The network automatically chooses a minimum-distance station tree and runs services in both directions. Passengers transfer between shuttles where necessary.
- Passenger arrivals supply immigration to the station's local road component. Local services carry sampled household commutes when walking to and from stations is shorter than the direct road distance. Access and egress walking time, waiting, transfers and train time contribute to travel duration.
- Cargo terminals receive up to 120 imported units. Existing trucks distribute goods to shops; imports are charged on successful delivery. Industry sends exports by truck to the terminal, then by train outside the map; export proceeds are credited on completion. Failed shipments return cargo to a surviving source. Roads remain a fallback where an outside road connection exists.
- Trains consist of a locomotive and three carriages. Initial capacity is 120 passengers or cargo units; speed is 24 world units/second; station dwell is five seconds. Services have a minimum 60-second departure interval, with additional waiting for occupied track blocks or station approaches.

The inspector shows operating problems, regional connection, arriving passengers, imported stock and outbound cargo. The Railway panel shows active trains and transported passengers/cargo; the budget includes track and facility maintenance.

## Prices

| Construction | Cost | Upkeep per simulated minute |
| --- | ---: | ---: |
| Double track | $40/tile | $0.02/tile |
| Passenger station | $4,000 plus new platform track | $40 |
| Cargo terminal | $6,000 plus new siding track | $60 |
| Depot | $5,000 plus new siding track | $50 |
| Highway overpass | $3,000 including track | $10 plus track upkeep |

Rail facilities unlock immediately. Regional track has no city upkeep. Sandbox construction follows the existing unlimited-money rules.

## Persistence and threading

City save version 7 stores rail links, protected tiles, bridge elevation and identity, train consists and progress, dispatch clocks, passenger transfers, station arrivals and export shipments. Versions 1-6 remain readable. Loading uses a candidate city and validates before publication. Crossing gates are reconstructed from active train positions.

The main simulation thread owns train movement, block reservations, crossing decisions and city transfers. Rail searches execute on the shared NetworkWorker using immutable world/station snapshots, one source search per job so road routing can interleave. Queue saturation retries without blocking gameplay; obsolete results are discarded. Render packets own vehicle instances and rail-aware world snapshots, including changes across dropped frames.

## Validation

Build with `scripts/build.ps1`. `RailwayTests` checks protected generation, transactional construction, rotation and footprint occupancy, overpasses, passenger transfers, depot requirements, crossings, save/load, rail-only city growth and trade, queue saturation, route invalidation and vegetation clearance.

Run `scripts/verify-railway.ps1` for the headless tests and DirectX validation/captures in both rendering modes. `--railway-scenario` creates a demonstration city with an overpass, crossing, stations, cargo terminal and depot. Manual routes, tunnels, arbitrary bridges, station upgrades and player-placed signals are outside this version.
