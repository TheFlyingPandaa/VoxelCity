# VoxelCity: plan for Cities: Skylines II-style parity

Date: 2026-09-13. Status: proposed roadmap, not an implementation commitment.

**Scope exclusion:** Audio, VFX and UI require a full separate rework and are excluded from this plan, its backlog and its acceptance gates. This includes sound/music, visual effects, interfaces, overlays, inspectors, charts, construction previews, tutorials, controls and presentation tooling. Terrain, weather and disasters below cover simulation mechanics only. Existing world geometry/rendering integration and performance checks remain engineering requirements; they do not prescribe the separate rework.

## 1. Target and scope

Build a voxel city builder in which players can shape terrain and transport networks, grow distinct neighborhoods, manage a believable population and economy, at metropolitan scale. Preserve the C++20/DirectX 12 foundation.

Use Cities: Skylines II's documented base-game systems as the comparison baseline. The official feature diaries cited below describe the intended feature set; they are not a verification of every mechanic in the September 2026 build. Paid expansions, every subsequent patch addition, exact asset counts, and numerical simulation equivalence are outside this initial parity contract. Refresh the comparison at each release-planning gate.

Define two delivery targets:

- **Core city-builder parity:** flexible construction, meaningful housing and employment, multimodal journeys, municipal management, and a tested 100,000-resident city. This population is a proposed VoxelCity product target, not a claim about CS2's limits.
- **Broad base-game parity:** add the remaining transport modes, full environment and service breadth, deeper progression, tourism, varied content, map/content data pipelines, and technical release validation. These targets cover the systems in this plan; overall product parity also depends on the separately scoped rework.

Keep Windows as the initial platform. Multiplayer, console ports, destructive voxel physics, photorealistic art, and a replacement engine are separate projects. Exact duplication of another game's assets or formulas is unnecessary.

## 2. Actual starting point

This assessment uses the working tree, including existing uncommitted railway and density work. It is a source/document review; no fresh executable validation was performed for this plan. Existing benchmark results are historical evidence, not new measurements.

| Area | Current evidence | Implication |
|---|---|---|
| Simulation | [City.h](../src/City.h), [City.cpp](../src/City.cpp): stable IDs, aggregate households, workplaces, owned trips, inventories, fixed ticks | A useful management loop exists; an aggregate household's members follow its building's resident count, so this is not yet a family or individual population model |
| Construction | [World.h](../src/World.h), [density/roads guide](DENSITY_AND_ROADS.md): 512 x 512 tiles, eight-direction connections, road classes, wider footprints, fixed roundabouts/interchanges | Extend the tools, but replace tile adjacency as the authoritative representation before arbitrary curves and stacked networks |
| Development | Low/high residential and commercial, industry, two building levels, shared RCI demand/taxes | High density is already implemented; medium density, mixed use, offices and distinct housing markets remain |
| Rail | [Railway guide](RAILWAY.md), [RailCity.cpp](../src/RailCity.cpp), [Rail.cpp](../src/Rail.cpp): regional trains, automatic local shuttles, transfers, freight terminals, crossings and a fixed overpass | Rail is a foundation for player-managed transit, not an absent feature; manual lines and general rail elevation remain gaps |
| Services/economy | Component-based utilities, coverage/dispatch services, taxes, upkeep, single goods inventory and emergency loan | Need network bottlenecks, institutional capacity, household/company accounts and multiple resources |
| Persistence | City format v7; migration paths for older cities and road-only imports; atomic replacement and candidate validation | Preserve compatibility through deliberate migrations; README/MVP references to v5 are stale |
| Concurrency | [NetworkWorker.h](../src/NetworkWorker.h), [threading guide](THREADING.md): one bounded network worker, main-thread gameplay, render-thread ownership | Retain snapshot ownership; profile before expanding the job system |
| Rendering integration | [renderer guide](VOXEL_RENDERER.md): voxel tracing, procedural geometry, vegetation, legacy rendering path | Preserve geometry compatibility and performance as simulation systems expand |
| Verification | Nine CMake test targets, including city, traffic, rail, threading and GPU checks | Extend the existing harness. Historical 50k/100k vehicle tests do not prove metropolitan simulation performance |

The [MVP status](CITY_MVP.md) and README were reconciled by BASE-01 on 2026-09-13; see the [source/test baseline](BASELINE.md) for fresh Debug/Release results and a snapshot of the working tree. The remaining assessment above is the original roadmap review. The outstanding two-hour real-time rendered soak remains a technical gate unless fresh evidence closes it. The new-player study belongs to the separate interface rework.

## 3. Parity gaps and destination milestones

The CS2 reference column summarizes official sources; the proposed implementation and gates below are VoxelCity design recommendations.

| Domain | CS2 comparison | VoxelCity work | Delivery |
|---|---|---|---|
| Roads | Curves, elevations, tunnels, ramps, road customization and parking ([road tools](https://www.paradoxinteractive.com/games/cities-skylines-ii/features/road-tools)) | Spline segments, lane connections, arbitrary junctions, grade separation, upgrades, pedestrian paths and parking | M1–M3 |
| Traffic | Route choice responds to journey costs and network conditions ([traffic AI](https://www.paradoxinteractive.com/games/cities-skylines-ii/features/traffic-ai)) | Shared journey planner, lane choice, parking search, congestion-dependent routing, intersection controls and incident recovery | M2–M3, M7 |
| Public/cargo transport | Multiple passenger and freight modes with player-created lines ([transport](https://www.paradoxinteractive.com/games/cities-skylines-ii/features/public-cargo-transportation)) | Manual rail lines, buses, stops, depots, fares and frequency first; tram, metro, taxi, passenger/cargo water and air transport later | M3, M7 |
| Housing and jobs | Residential variety, mixed use, offices and building development ([zones](https://www.paradoxinteractive.com/games/cities-skylines-ii/features/zones-signature-buildings)) | Multi-cell lots, six housing categories, low/high offices, household tenancy, occupancy, affordability and redevelopment | M2, M4 |
| Citizens | Individual lifepaths and activities ([citizens](https://www.paradoxinteractive.com/games/cities-skylines-ii/features/citizen-simulation-lifepath)) | Age, family membership, education, work, shopping, leisure, illness, retirement, births/deaths and migration | M4 |
| Economy | Households and companies participate in production and consumption ([economy](https://www.paradoxinteractive.com/games/cities-skylines-ii/features/economy-production)) | Wages, rent, expenses, company accounts, commodity chains, warehousing, outside trade, loans and reconciled accounting | M4–M5 |
| Services/governance | Service budgets/upgrades, districts and policies ([services](https://www.paradoxinteractive.com/games/cities-skylines-ii/features/city-services-districts-policies)) | District boundaries, service assignment, staffing/fleets, education tiers, deathcare, maintenance, post/telecom, utility bottlenecks | M5, M7 |
| World/environment | Maps, themes, climates and seasons ([feature overview](https://www.paradoxinteractive.com/games/cities-skylines-ii/features)) | Terrain/water, land purchase, resources, regional connections, pollution transport, seasonal demand and hazards | M1–M2, M7 |
| Progression | Milestones and development choices ([progression](https://www.paradoxinteractive.com/games/cities-skylines-ii/features/game-progression)) | Branching unlocks, city specialization, signature rewards, tourism and late-game goals | M5, M7 |

## 4. Architecture to establish before feature expansion

### World and transport representation

Introduce stable network node/segment/lane IDs with horizontal curves, elevation profiles, travel permissions and junction movements. Keep tile/chunk occupancy for spatial queries, voxel generation and compatibility, but derive routing from network geometry. Curved frontage needs explicit parcels and entrances rather than a single building tile.

Start by converting existing straight roads into equivalent segments. Preserve protected regional ownership, road prices, rail crossings, interchange behavior and demolition rules. Derive rendering and routing from the same accepted geometry. Model grade-separated crossings explicitly so visual overlap never creates a connection.

Add chunk-owned terrain height/material data, water state and resource fields. Define a consistent world-unit scale before slope, walking speed, catchment or map-area claims. A first surface-water implementation can use a coarse heightfield flow grid; full volumetric fluid simulation is not required. Tunnels need portal/clearance and rendering rules, not destructible terrain throughout the world.

### Population, businesses and accounting

Separate `Citizen`, `Household`, `Dwelling`, `Building`, `Company` and `Job` identities. Several households and companies must be able to share a building. Preserve aggregate reporting as derived state. Create deterministic age/household distributions when migrating old aggregate saves; document that old saves did not contain recoverable individual histories.

Retain physical ownership of trips and cargo. Add resource type, quantity, payer/payee and transaction identity. Municipal finances must be distinct from private import/export receipts: the existing treasury-based trade model needs an explicit migration rule. Every transfer must settle once or have a recorded failure/refund path.

### Journey planning and time

Represent a journey as walking, parking, driving and transit legs with transfer state, reservation rules and an owning citizen/shipment. Introduce this contract using existing aggregate trips before individual citizens arrive. Generalized route cost should expose time, fare, parking, waiting and transfer penalties; tune them with fixtures.

Specify the calendar before adding daily routines: map simulation ticks to clock time and separately define aging, construction and financial periods. Avoid spawning every worker simultaneously or aging residents at the same cadence as vehicle motion.

### Ownership, data and saves

Extract focused simulation modules from `City.cpp` as those systems change. Preserve one authoritative mutation owner and immutable worker snapshots. Add generation IDs, cancellation and fair job scheduling; parallelize measured hot spots with deterministic reduction order where needed. Do not claim ordinary asynchronous play is bitwise deterministic: current worker completion timing can affect outcomes. Keep a reproducible headless mode and separately test normal scheduling.

Replace positional building rows and enum-dependent content indexing with versioned, named definitions and stable content IDs. Keep saved definitions or compatible content hashes. Introduce versioned save sections and explicit migrations without replacing atomic write and candidate-load validation. Cache data is rebuildable; financial balances, agents and in-flight cargo are authoritative.

## 5. Milestone roadmap

The previous estimates included work now excluded from this plan and are withdrawn. Re-estimate the remaining simulation, geometry, persistence, content-data and testing work after M1, using confirmed staffing. These milestones describe system capabilities; interactive delivery depends on the separately scoped rework.

| Milestone | Depends on | System outcome |
|---|---|---|
| M0: trustworthy baseline | — | Current city is reproducibly buildable, measurable and documented |
| M1: geometry and data foundations | M0 | Old cities run on the new network/terrain contracts |
| M2: construction geometry | M1 | Construction commands support varied neighborhoods on non-flat land |
| M3: connected journeys | M2 | Bus and rail networks compete meaningfully with driving |
| M4: residents and housing | M3 journey contract | Real households choose homes, education and employment |
| M5: economy and city government | M4; resource fields from M1 | A city survives through production, budgets and service decisions |
| M6: metropolitan core validation | M2–M5 | Core parity demonstrated at 100,000 residents |
| M7: broad feature/content parity | M6 | Remaining modes, environment, services and endgame integrated |
| M8: content pipeline and technical validation | M7; authoring foundations from M1 | Validated content data, packaging and compatibility |

Schedule the system work after interfaces and staffing are confirmed. A solo implementation remains a multi-year program; deliver and validate the core systems incrementally.

### M0 — Baseline and measurement

- Reconcile README/MVP with v7 saves, densities, rail and current test targets. Record a reproducible source snapshot without discarding existing working-tree changes.
- Run Debug/Release suites and applicable city, railway, threading and renderer scripts. Capture existing save fixtures and benchmark hardware/settings.
- Instrument city-step cost by subsystem, route queue age, simulation time achieved, command-processing latency, RAM and VRAM.
- Complete the documented rendered soak; track failures as technical release-blocking issues.

**Exit:** one reproducible baseline report, passing current correctness suites, documented known issues, and fixture saves covering every currently supported format.

### M1 — Foundations and migration slice

- Build the segment graph, terrain storage, spatial indexes, stable content IDs and save-section migration infrastructure.
- Convert a starter city and its active traffic/rail trips; retain old loaders behind a migration adapter.
- Prototype one curve, one sloped road and one overpass through price, occupancy, routing, voxel rendering and save/reload.

**Exit:** migrated fixtures preserve balances, population, reachability and cargo ownership; crossing roads at different heights remain disconnected. Failed construction changes neither money nor occupancy. M1 is the architecture go/no-go gate before expanding content.

### M2 — Construction and urban form

- Implement geometry and validated construction commands for straight, curved, freeform, grid and parallel roads, upgrades, ramps, bridges and tunnels.
- Add terrain raise/lower/level/smooth, water placement rules, playable map presets and purchasable land regions.
- Add multi-cell parcels, lot/frontage generation, zoning region operations and redevelopment rules; preserve existing low/high zones.
- Add pedestrian paths/crossings and parking geometry/state contracts for M3. Provide reversible construction transactions where simulation has not advanced.

**Exit:** build and reload a river town containing a curved arterial, sloped street, bridge, tunnel, custom interchange and irregular blocks. Automated checks reject invalid slopes, overlapping lots and occupied bridge clearances; validated construction commands apply the expected cost and geometry.

### M3 — Traffic and player-managed transit

- Generalize automatic rail service data into editable routes/stops, retain automatic service as an optional operating mode, and add buses/depot fleets.
- Implement end-to-end journeys, walking access, finite vehicle capacity, transfers, fares, frequency and parking demand.
- Track stop queues, line profitability, mode share and travel-time metrics in simulation state and test reports. Improve lane/junction movement and emergency access where fixtures reveal failures.

**Exit:** on a fixed corridor, adding a useful bus/rail line reduces car journeys and generalized travel cost; an overcrowded line records denied boardings. Route deletion, road edits and save/load lose no travelers or cargo. A disconnected destination records a failed journey without creating a false successful trip.

### M4 — Citizens, housing and employment

- Add persistent individual citizens and family households, age distribution, education, health, employment and scheduled activities.
- Implement row housing, apartments, low-rent housing, mixed use and offices with distinct demand and occupancy. Add rent, household budgets, job qualifications and moving decisions.
- Replace full reassignment of employment each city step with persistent contracts and bounded job/home searches.
- Record unmet needs and journey decision factors for behavioral validation.

**Exit:** controlled tax, housing-cost and school-access scenarios produce explainable differences in affordability, education and employment. Births/deaths/migration reconcile population; mixed-use tenants and active journeys survive demolition and save/load without dangling references.

### M5 — Economy, utilities and governance

- Start with a small explicit resource chain (raw material → manufactured goods → retail consumption), plus services produced by offices; expand via data after conservation passes.
- Add extraction areas, warehouses, contracts, production inputs, wages, private accounts and outside-market trade. Migration must not invent a municipal windfall.
- Add district boundaries/policies, service budgets, operating staff and upgrades, education tiers, deathcare and maintainable municipal debt.
- Replace utility component totals with capacity-constrained distribution, including road-carried low-capacity links and explicit trunk connections. Represent disconnected, overloaded and underfunded states separately in simulation data.
- Implement milestone and development-unlock rules with distinct gameplay consequences.

**Exit:** every commodity and monetary transfer reconciles to a named source/sink; interrupted freight never duplicates stock or payments. Grid bottlenecks cause local shortages despite sufficient citywide capacity. Several city layouts can remain solvent without scripted growth or hidden subsidies.

### M6 — Core parity and scale

- Profile 10k, 25k, 50k and 100k organically active populations. Optimize indexes, update scheduling, route caching, worker fairness and render snapshots before expanding map limits.
- Keep individual state persistent while rendering only necessary detail. Distant simulation may update less often only with documented, tested error bounds; visible traffic remains tied to real demand.
- Validate start-to-metropolis progression with scenario runs and balance checks.

**Exit:** all core systems meet the shared gates below; publish the remaining broad-parity gaps explicitly.

### M7 — Breadth, environment and late game

- Add tram, metro and taxi service, then passenger/cargo ports and airports, using the shared journey and shipment contracts.
- Add remaining service categories: post, telecom, road maintenance, advanced health/education and emergency facilities, with capacity and operating costs.
- Add transported ground/air/water pollution, resource depletion/renewal rules, a day/night clock, seasonal demand and weather-dependent traffic conditions, and bounded disasters with prevention/recovery.
- Add tourism, hotels, attractions, signature rewards and specialization goals. Grow a coherent catalog of building footprints, growth stages, vehicles and architectural themes.

**Exit:** each new mode has a useful scenario and end-to-end journey tests; each service/hazard produces measurable, reversible consequences. A port/airport city, dense transit city and industrial/resource city all support extended play without breaking economy or performance gates.

### M8 — Content pipeline and technical validation

- Support map, building and vehicle definitions through versioned file formats and command-line validation, packaging, dependency/version checks and missing-content recovery. Start with data/content extensions; general code plugins require a separate API/support decision.
- Set an agreed content coverage matrix by zone, density, level, footprint and theme; review district-scale repetition as well as individual asset quality.
- Test clean installation, upgrades, corrupted saves, long sessions and supported hardware; publish the final feature matrix and limitations.

**Exit:** documented example map and asset definitions validate, package and load successfully; package and compatibility gates pass.

## 6. Shared acceptance and performance contract

These are proposed budgets to validate in M0/M1 and tighten with measurements, not claims that current code achieves them at the proposed scale.

| Gate | Proposed requirement |
|---|---|
| Correctness | No duplicate IDs, negative inventory, double settlement, orphaned journeys or invalid references across edits, cancellation, migration and reload |
| Regression | Existing Debug/Release suites remain passing; add behavioral fixtures for each new subsystem and adversarial network edits |
| Reproducibility | Fixed-seed headless runs produce repeatable results in controlled worker mode; normal asynchronous runs preserve invariants under delayed/saturated workers |
| Small city rendering | Preserve the documented 1440p/60 target on RTX 4070 at 10k residents; p95 frame time ≤16.67 ms in representative views |
| Metro rendering | Proposed 100k target: 1440p Medium p95 ≤33.3 ms on the reference machine, with both GPU and presentation timing reported |
| Simulation | At 100k, sustain 30 simulation ticks per wall-clock second at 1×; report achieved/requested ratio at 2×/3×. Benchmark reports must distinguish requested speed from achieved simulation throughput |
| Command processing | Proposed construction-command processing p95 ≤100 ms; no synchronous network rebuild or asset upload stalls during ordinary construction |
| Memory | Initial 100k budget: process working set ≤12 GiB and VRAM ≤8 GiB; no sustained growth after warmup across repeated edit/save cycles |
| Soak | Two-hour real-time rendered session with camera movement, growth, demolition and save/reload; no crash, corruption, deadlock or persistent unbounded queue growth |

Benchmark ordinary city traffic separately from injected stress trips. Record population, active/pending journeys, completed trips, mode share, simulated time, camera, resolution, quality, hardware and source revision. Collect median/p95/p99 over repeated runs after warmup; exclude capture overhead from timing. An empty-map renderer result cannot satisfy the metro gate.

## 7. Risks and scope control

| Risk | Mitigation and decision |
|---|---|
| Tile assumptions permeate routing, rendering and saves | M1 must prove a migrated playable slice before M2. If the adapter is too costly, revise the architecture and estimate at that gate |
| Individual agents overwhelm main-thread/route budgets | Benchmark realistic activity at each population tier; stagger decisions, cache paths and partition jobs. Defer the population claim if measured fidelity cannot meet its budget |
| Coupled housing/economy becomes unstable or opaque | Introduce one feedback loop at a time; log causal factors, use bounded adjustment rates and retain diagnostic scenario controls |
| New systems corrupt old saves | Maintain immutable historical fixtures, explicit migration tests and candidate publication; never overwrite the original during migration |
| Broad asset requirements dominate engineering progress | Build the definition/authoring pipeline early and grow a reviewed coverage matrix; avoid hundreds of bespoke hardcoded assets |
| Feature count displaces correctness | Every milestone must include a reproducible scenario and behavioral assertions; stop expansion when its acceptance gate fails |
| Hardware scope expands unexpectedly | Retain current DXR requirements for the initial contract. Wider GPU/platform support requires a separately measured renderer plan |
| Reference game continues changing | Freeze the above base-system contract for implementation and log later additions separately |

If staffing or time is constrained, ship M0–M6 as the explicit core target and postpone M7/M8 breadth. Do not remove save safety, simulation correctness or performance validation to retain more feature names.

## 8. First implementation backlog

Work in this order; each item should become a bounded task with its own reviewable result.

1. **BASE-01 — completed 2026-09-13:** reconciled feature/save documentation and recorded the [current source/test baseline](BASELINE.md), preserving existing work. Debug and Release each pass all nine tests; broader M0 gates remain open.
2. **PERF-01:** add subsystem and route-age metrics to the existing benchmark reports, with representative 10k and higher-population fixtures. Report failures honestly rather than raising advertised limits.
3. **SAVE-01:** collect v1–v7 fixtures appropriate to each historical format and add migration/conservation assertions around `CitySimulation::load`.
4. **NET-01:** write the node/segment/lane and unit-scale design, then implement one straight-road adapter with topology equivalence tests.
5. **WORLD-01:** prototype terrain elevation and a grade-separated crossing through construction, routing, rendering and persistence.
6. **DATA-01:** introduce stable content identifiers and a converter for current positional building definitions, preserving saved tuning.
7. **BUILD-01:** validate road cost, slope, clearance and transactional rejection in the authoritative construction command layer, with headless tests.
8. **GATE-01:** review the M1 slice, migration results and performance evidence; update the remaining estimates before commencing the full road/transit expansion.

This sequence addresses the largest structural dependencies first while leaving the current playable city intact throughout development.
