# VoxelCity quick start

Launch `VoxelCity.exe`. Start with the guided starter layout through **New / starter city**, or extend the local access road on the new highway map.

1. Extend the short local road at the **regional highway junction**. The highway links Westhaven and Eastbridge beyond the map edges, carrying through traffic, imports and exports. Use **Highway junction** in the overview to find it.
2. Put a power plant, water tower and sewage treatment facility next to roads on that network. Utilities travel through roads; separate pipes and wires are unnecessary.
3. Paint residential homes, commercial shops and industry beside roads. Buildings develop when demand and supply permit. Homes attract residents; businesses supply jobs.
4. Open **Budget** to inspect RCI demand, utility capacities, taxes and income. Financial settlement happens every simulated minute. Roads and facilities cost money immediately.
5. At 500 residents, provide garbage collection, healthcare, fire protection and parks. At 2,000, add schools and police. Buildings can upgrade when their services, happiness and land value support it.
6. Use **Inspect parcel** and overlays to diagnose warnings. Add road connections and shorter routes for service vehicles and freight. Long blocks can make a nearby facility inaccessible within its road-distance coverage.
7. Expand toward 10,000 residents, adding utility capacity and jobs as needed.

| Control | Action |
|---|---|
| Left-click / drag | Apply the selected tool |
| Right-click / drag | Bulldoze, costing $5 per parcel/road tile |
| Middle drag | Pan |
| Alt + middle drag | Orbit |
| Wheel | Zoom |
| Pause / 1× / 2× / 3× | Simulation speed |
| Save city / Load city | Native `.vcity` dialogs |

The highway has separate one-way carriageways, a grass median and shoulders. Its region-owned lanes cannot be bulldozed or redirected and do not charge municipal upkeep. Connect at the central junction; roads painted against a shoulder do not connect, and buildings need local-road frontage. New maps and the starter/town/metropolis scenarios include it; loading an older city preserves that city�s layout.

Road direction and signal tools modify existing local roads. Direction arrows appear on one-way roads. Signals alternate north/south and east/west approaches. Erasing a connecting road can disconnect utilities and interrupt trips; reconnect it to allow recovery.

Households represent groups of residents. Commuting is sampled; freight and dispatched services are physical trips. Trips can wait for safe insertion or routing work. Persistent congestion is a problem to solve, not a reason for vehicles to vanish.

**Recovery:** three autosaves live in `%LOCALAPPDATA%\VoxelCity\Autosaves`. Use **Load city** to open one. Autosaves run every five real-time minutes while unpaused. Save/discard/cancel protects closing a modified city. Keep the executable's `shaders` and `data` folders beside it.

**Money:** one emergency loan supplies $25,000 and adds $250 interest each simulated minute. Three consecutive negative-treasury financial periods pause the city. Load an earlier save, restart, take an unused loan or choose unlimited-money sandbox. Imports and exports also change cash when their deliveries arrive; the budget's recurring balance excludes these variable trade transactions.

**Current boundaries:** Windows x64 with AVX2 and DXR 1.1 support. Flat grid roads only. No public transport, curved roads, bridges, terrain editing, districts or individual citizen schedules in this MVP.

## Voxel graphics

The game starts with voxel ray tracing and High lighting quality. Low and Medium reduce secondary-light sampling and tracing distance. The build grid starts hidden; enable it in the build panel when useful for construction. Buildings and moving cars now receive and cast soft shadows, with material-dependent reflections and bounced daylight.

Use `--renderer legacy` to compare the old mesh renderer. City saves and gameplay rules are unchanged. The graphics target is 1440p at 60 FPS on an RTX 4070; see `VOXEL_RENDERER.md` for measured scenarios.
