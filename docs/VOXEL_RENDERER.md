# Voxel renderer â€” 0.3.0

VoxelCity now uses actual voxel ray tracing by default. The implementation is inspired by Teardown's voxel geometry and realistic lighting, with DXR acceleration chosen for this project's RTX 4070 / 1440p target. It is an independent renderer, not Teardown engine code or an exact reproduction of its original software tracing pipeline.

## Geometry and scene updates

`Voxels.cpp` generates renderer-only data from the existing world and parcel descriptions. Terrain columns are compressed into homogeneous solid runs. Logical raised columns render at 0.25 world units in both renderers, matching building foundations; road masks and saved layouts retain their original representation. The voxel road model also includes occasional sidewalk lamp fixtures, rebuilt with road chunks and excluded from highways. They do not change logical columns or add nighttime illumination. Building walls and trees use quarter-world-unit voxels; vehicles and factory roof assemblies use eighth-world-unit voxels. Main residential roofs use eighth-unit cells across their slopes and quarter-unit cells along their ridges, with exactly repeated ridge bricks joined into longer primitives. Factory roof bricks merge into the building with adjusted packed-data offsets and vertical bounds. Sparse traversal derives cell dimensions from each brick's bounds, so both resolutions share the same format. Palette indices are packed four palette indices per 32-bit word in 8Ã—8Ã—8 bricks. Empty bricks are omitted and homogeneous bricks become constant runs. The upper byte of a nonuniform brick's data field holds a representative material for conservative distant visibility; the lower 24 bits hold its model-relative word offset.

DXR bottom-level acceleration structures contain procedural bounding boxes, not triangle geometry. Inline ray queries invoke the shader's slab intersection and bounded voxel DDA. Instanced models share both their packed data and acceleration structures. A common empty-terrain model covers the map without storing a dense world volume. Only dirty terrain chunks and new building variants regenerate model data; live model data is repacked after geometry changes, so editing does not leave an ever-growing voxel allocation. Fence-protected scene and upload buffers are reused during painting, and vehicle pose caches retain allocations between frames. Terrain edits are synchronous; very large edits or loading a dense road map can pause while their chunks are regenerated.

Terrain chunks with byte-identical primitive records and packed voxel words share a GPU model. A sampled hash selects candidates; full comparison verifies every match. Dirty chunks whose geometry is unchanged retain their existing model. The cache holds weak references and removes expired entries periodically; packed scene data includes each shared model only once. Chunk instance identities and transforms remain separate, preserving motion/history validation and world-space materials.

All terrain, trees, buildings, and active vehicles remain available to light rays, including offscreen objects. Leaf backlighting allows a bounded 0.75-world-unit span of foliage, checking other materials within that span and all occluders beyond it. A single scene query and distance-limited brick traversal handle both intervals. GPU conformance compares this rule against split CPU reference intersections. This is a thin-foliage approximation, not volumetric scattering. Interpolated car poses update each rendered frame. A renderer-only vehicle ID supports motion reconstruction without changing serialized traffic data. IDs are unique among live objects and exactly representable in the surface buffer; exhausting their range clears history before reassignment. Building IDs derive from parcel coordinates; world changes invalidate temporal history. Coarse occupied-brick visibility is used only when detail is subpixel; lighting rays continue to use exact voxel intersections.

`VoxelRenderer.cpp` owns its GPU resources separately from the retained legacy mesh path. Uploads, retired resources and acceleration structures respect the existing three-frame fences. Resize waits for the GPU before replacing render targets. The legacy renderer does not allocate the new voxel render targets until voxel mode is used.

## Frame pipeline

1. Update scene instances and changed models; build the frame's top-level acceleration structure.
2. Trace fixed pixel-center primary rays at native resolution at every quality level into position/identity, normal/material, albedo/roughness, and motion buffers. Static edges remain stable without a frame-dependent sampling offset. Camera rays are reconstructed relative to the eye to avoid cancellation at large world coordinates.
3. Trace half-resolution soft sunlight, occluded skylight, one diffuse bounce and roughness-dependent reflections. High quality integrates all four fixed primary samples in roof blocks crossing faces or boundaries; a single planar footprint reuses one lighting evaluation. Main residential roof metadata supplies a slope shading normal and a bounded (at most 0.25-unit) vertical lighting-origin lift to the stair envelope. Exact primary hit positions and silhouettes remain unchanged. Factory sawtooth slopes use their authored 0.5 pitch and a repeating four-unit run, with lift bounded at 0.125 units; exposed high ends and gable faces keep geometric normals. The narrow house uses its four-unit half-span and quarter-unit step envelope, with lift bounded at 0.5 units only above its main eaves; low flat roofs remain geometric. Instance records are 80 bytes and include ridge/eave data plus span, pitch, envelope offset and lift limit. The lift is stored in motion.w; secondary ray hits keep geometric normals. Secondary hits resolve their own voxel material and sun visibility. The diffuse bounce deliberately excludes specular caustics to avoid fireflies.
4. Reconstruct lighting with material-, normal- and geometry-aware spatial filtering. Matte roofs gather across adjacent treads and risers within a distance-limited footprint; other materials retain strict face-normal filtering. Reproject HDR history using previous camera and vehicle transforms; validate each bilinear history tap against identity, material and static position. Reject mismatched normals except on static roofs, preserving history across adjacent stair treads and risers during camera movement. These matte roof samples accumulate without current-face radiance clipping to stabilize their self-shadows. Limit moving-surface and moving-glass history to four samples, static roof and stationary-glass history to thirty-two and other static history to sixteen. A pane moving less than 0.05 pixels retains stationary history; stationary glass preserves radiance without current-sample clipping to reduce indirect-light noise. Reject all history on scene changes, camera cuts, resize, renderer switches and quality/shadow changes.
5. Apply distance haze, restrained bloom, exposure and tone mapping. Composite construction and service overlays, and render the UI after scene processing.

Material responses distinguish grass, asphalt, markings, concrete, brick, plaster, roofs, glass, metal, wood, foliage, rubber, paint and emissive lights. Asphalt grain fades with projected pixel footprint; sparse flush utility covers use filtered rings and ribs within the asphalt material. Their shading does not change terrain geometry or road simulation. Finished timber has footprint-filtered grain and board seams; tree trunks and limbs use a separate bark material. Static metal has restrained staining and oxidation; moving car hardware uses a separate polished finish without world-anchored surface noise. Industrial concrete uses panel variation and footprint-filtered joints and grain. Factory roofs use a separate weathered sheet-metal palette with filtered seams and the same roof lighting reconstruction as residential roofs. Building generators add recessed windows, trim, doors and roofs. One residential variant also has rooted climbing ivy with stems and leaf clusters outside the corner walls. Conifers use overlapping thin foliage sprays on tapered twig fans, leaving gaps between canopy layers. Service buildings include a supported water tank, sewage basins, power stacks, service doors/signage, and a tree-lined park. The school has a low classroom wing, a covered clock entrance and a paved courtyard, with desks and teaching boards inside. Physical footprints and simulation behavior remain unchanged.

## Controls and limits

- **High** (default): native primary visibility, half-resolution secondary lighting, two diffuse samples, 100-unit diffuse trace distance, 256-unit reflection distance.
- **Medium**: one diffuse sample, 64-unit diffuse distance and 256-unit reflections, earlier distant detail reduction.
- **Low**: one occlusion sample, 24-unit diffuse distance, 64-unit reflections; shaded surfaces omit the extra diffuse hit-light evaluation.
- `--renderer voxel|legacy`, `--quality low|medium|high`; `--no-shadows` disables sun visibility rays without disabling voxel rendering.
- The build grid starts hidden in voxel mode; it can be enabled from the build panel. Existing overlays, signals, arrows and hover feedback remain available.
- `--camera-view x y z yaw pitch distance` sets an optional capture pose after scenario defaults. Focus uses world units; yaw/pitch use radians. Pitch and distance stay within the existing camera limits. `--camera-path` orbits from this yaw when provided.
- `--camera-path` orbits deterministically for validation. `--no-ui` omits UI from rendering; `--no-grid` hides the construction grid.
- `--capture-sequence folder` writes numbered BMPs after the first 30 frames. Capture waits on the GPU and includes disk I/O: never use sequence captures as performance measurements.

Vertical building glazing and vehicle glazing combine a transmitted scene ray with a Fresnel reflection. Building models contain room floors, partitions and furnishings. Car cabins contain seats and dashboards behind thin stepped panes. Primary windscreen shading uses the model's overall 45-degree slope, transformed with the car pose, so tread faces do not create alternating reflection bands. Intersections and silhouette remain voxel-based; this shading rule is specific to the current car model. Horizontal building glass retains opaque shading; glass in secondary reflections is not recursively transmitted. Interior direct-light rays can pass through glass, and room illumination samples occluded skylight and one diffuse bounce (four samples on High, two on Medium, one occlusion sample on Low). Reflections beyond their trace distance use the sky; lighting is a sampled approximation, not an unlimited path tracer. Fixed primary sampling removes subpixel temporal antialiasing, so distant edges can appear more jagged or shimmer during camera movement. Stochastic lighting and newly exposed surfaces can still show residual temporal noise. The voxel renderer uses fixed warm daylight aimed toward the street-facing facades and visual distance haze, with no weather, destruction, first-person mode or save migration.

## Validation

Builds run the four existing simulation suites plus two new suites:

- `VoxelTests`: packed-brick traversal compared with exhaustive occupied-cell intersections; empty, inside and axis-aligned rays; terrain column heights and chunk seams for all road connection masks; bounds and packed offsets for all generated building variants.
- `VoxelGpuTests`: 8,192 shader traversal rays compared with the CPU reference, with DirectX error checking. This suite requires a supported GPU and is labeled `gpu` in CTest.

Reproduce the rendered acceptance cases with:

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\build.ps1 -Configuration Debug
.\scripts\verify-voxel.ps1
.\scripts\verify-city.ps1
.\scripts\verify.ps1
.\scripts\verify-traffic.ps1
```

The voxel verification script checks native 2560Ã—1440 High at normal simulation speed in the 10,000-resident scenario, including close-up, overview and orbiting city views. The Release p95 frame budget is 16.67 ms. Reports distinguish full frame time, average GPU pass timings and peak process video memory. The 5,000-vehicle city stress and older 50,000/100,000-car diagnostic workloads are separate from that normal-city target.

See the results table below for the measured build. Startup and the first 30 rendered frames are excluded from timing; benchmarks disable VSync and sequence capture. Screenshots are captured only after the measured frames.

## Measured results — September 5, 2026

Windows, NVIDIA GeForce RTX 4070, Release 0.3.0, 2560×1440 High. Each normal-city camera benchmark uses 630 frames after simulation warmup, excluding the first 30 frames from timing. All cases below reported zero DirectX errors. The debug layer is disabled for performance cases except the explicitly marked painting test.

| Scenario | P95 frame ms | Average GPU ms | Peak video memory MiB |
|---|---:|---:|---:|
| 10,000 residents, close-up orbit | 8.056 | 7.268 | 538.2 |
| 10,000 residents, city orbit | 5.819 | 5.094 | 538.2 |
| 10,000 residents, whole-map orbit | 2.632 | 1.048 | 537.6 |
| Continuous road painting (debug layer) | 13.213 | 2.091 | 628.1 |
| 5,000+ active vehicles, 1x | 6.524 | 5.452 | 578.9 |
| 5,000+ active vehicles, 3x | 10.727 | 5.399 | 578.9 |
| 50,000-car diagnostic, overview | 22.742 | 3.557 | 598.4 |
| 100,000-car diagnostic, overview | 38.359 | 6.778 | 676.7 |

All three normal-city camera cases meet the 16.67 ms budget. The older 50,000/100,000-car diagnostics remain CPU-heavy and do not meet 60 FPS; these are not the normal 10,000-resident workload. The city stress runs retain the original 16.67 ms (1×) and 33.3 ms (3×) budgets and pass both.

Both Debug and Release pass all six CTest suites. Construction input, road painting/erasing, city input, resize/minimize/restore, all-map road scenarios, Low/Medium/High, `--no-shadows`, and the legacy comparison path pass rendered validation. Reusing fence-protected scene buffers removed the full-scene allocation/retirement bottleneck during painting. Persistent vehicle pose caches also reduced overhead in the large-car diagnostics.

Full reports and captures are in `artifacts/`. The voxel-memory field counts model BLAS and logical packed model data; peak video memory additionally includes render targets, frame copies, scratch resources and other application allocations. CPU stage fields are the last measured frame; GPU stage fields are averages over the measured frames.

## Technical references

- [Teardown's official rendering FAQ](https://www.teardowngame.com/faq.html)
- [Dennis Gustafsson: From screen space to voxel space](https://blog.voxagon.se/2018/10/17/from-screen-space-to-voxel-space.html)
- [Dennis Gustafsson: Year summary, including the distinction between Teardown and later renderer work](https://blog.voxagon.se/2024/12/29/year-summary.html)
- [Microsoft: DXR specification, procedural inline ray queries](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)

Close-up screenshots and sampled frames from the 180-frame orbit capture were visually reviewed. The preview image is artifacts/voxel-preview.png and the six-second motion clip is artifacts/voxel-motion.mp4. These are local validation artifacts, not performance recordings.


## Environmental trees (2026-09-06)

`Vegetation.cpp` derives scattered groves from seed 1949 and tile coordinates, independently of simulation randomness. Four shared broadleaf/conifer models use wood and leaf voxels; the legacy renderer instances matching box meshes. Canopies stay inside their tiles. Both renderers cache placement per chunk and keep offscreen trees in the ray tracing scene. Static tree identities occupy `0x100000 + tile`, below the vehicle identity range.

Successful construction marks road tiles, diagonal reservations, full roundabout footprints and parcels in a 32 KiB cleared-tile mask. A separate vegetation revision and per-chunk revisions travel with immutable world snapshots, so skipped render packets cannot restore cleared trees. City save version 5 appends generation version 1, a 32-bit seed and the mask after traffic state; the existing length/checksum and candidate-load validation cover the block. Versions 1–4 initialize vegetation from existing occupancy. Diagnostic road saves keep their old format and disable vegetation.

Release spot measurements on RTX 4070 at 2560×1440, High, 120 frames, town scenario (short runs, not a full performance qualification):

| Camera | Before p95 | With trees p95 | Before video MiB | With trees video MiB |
|---|---:|---:|---:|---:|
| Close-up | 6.849 ms | 7.140 ms | 518.820 | 561.051 |
| Whole map | 1.389 ms | 1.846 ms | 518.258 | 561.051 |

Reports and captures: `artifacts/trees-before-{close,overview}.*`, `artifacts/trees-after-{close,overview,legacy-close,legacy-overview}.*`. Reproduce with `--benchmark --city-scenario town --frames 120 --no-ui --close-up` (or `--overview`), adding `--report` and `--capture`. `artifacts/trees-landscape*.bmp` shows the new highway landscape and close-up trees in both renderers; these runs enabled the DirectX debug layer and reported zero errors.

`VegetationTests` checks deterministic placement, bounds, construction clearing, failed commands, persistence, old-save migration and malformed vegetation rejection. GPU traversal validation includes all four tree models alongside the existing car/empty-brick cases.
`scripts/build.ps1 -Configuration Release` passed all eight suites. `scripts/verify-threading.ps1` passed delayed voxel/legacy rendering, city input, resizing, edits and capture-sequence checks with zero DirectX errors.
