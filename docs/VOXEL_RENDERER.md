# Voxel renderer â€” 0.3.0

VoxelCity now uses actual voxel ray tracing by default. The implementation is inspired by Teardown's voxel geometry and realistic lighting, with DXR acceleration chosen for this project's RTX 4070 / 1440p target. It is an independent renderer, not Teardown engine code or an exact reproduction of its original software tracing pipeline.

## Geometry and scene updates

`Voxels.cpp` generates renderer-only data from the existing world and parcel descriptions. Terrain columns are compressed into homogeneous solid runs. Detailed models use quarter-world-unit voxels, packed four palette indices per 32-bit word in 8Ã—8Ã—8 bricks. Empty bricks are omitted and homogeneous bricks become constant runs. The upper byte of a nonuniform brick's data field holds a representative material for conservative distant visibility; the lower 24 bits hold its model-relative word offset.

DXR bottom-level acceleration structures contain procedural bounding boxes, not triangle geometry. Inline ray queries invoke the shader's slab intersection and bounded voxel DDA. Instanced models share both their packed data and acceleration structures. A common empty-terrain model covers the map without storing a dense world volume. Only dirty terrain chunks and new building variants regenerate model data; live model data is repacked after geometry changes, so editing does not leave an ever-growing voxel allocation. Fence-protected scene and upload buffers are reused during painting, and vehicle pose caches retain allocations between frames. Terrain edits are synchronous; very large edits or loading a dense road map can pause while their chunks are regenerated.

All terrain, buildings, and active vehicles remain available to light rays, including offscreen objects. Interpolated car poses update each rendered frame. A renderer-only vehicle ID supports motion reconstruction without changing serialized traffic data. IDs are unique among live objects and exactly representable in the surface buffer; exhausting their range clears history before reassignment. Building IDs derive from parcel coordinates; world changes invalidate temporal history. Coarse occupied-brick visibility is used only when detail is subpixel; lighting rays continue to use exact voxel intersections.

`VoxelRenderer.cpp` owns its GPU resources separately from the retained legacy mesh path. Uploads, retired resources and acceleration structures respect the existing three-frame fences. Resize waits for the GPU before replacing render targets. The legacy renderer does not allocate the new voxel render targets until voxel mode is used.

## Frame pipeline

1. Update scene instances and changed models; build the frame's top-level acceleration structure.
2. Trace jittered primary rays at native resolution into position/identity, normal/material, albedo/roughness, and motion buffers. Camera rays are reconstructed relative to the eye to avoid cancellation at large world coordinates.
3. Trace half-resolution soft sunlight, occluded skylight, one diffuse bounce and roughness-dependent reflections. Secondary hits resolve their own voxel material and sun visibility. The diffuse bounce deliberately excludes specular caustics to avoid fireflies.
4. Reconstruct lighting with material-, normal- and geometry-aware spatial filtering. Reproject HDR history using previous camera and vehicle transforms; validate each bilinear history tap against identity, material and static position. Reject mismatched normals except on static roofs, where jitter alternates between adjacent stair treads and risers. These matte roof samples accumulate without current-face radiance clipping to stabilize their self-shadows. Limit moving-surface history to four samples, static roof history to thirty-two and other static history to sixteen. Reject all history on scene changes, camera cuts, resize, renderer switches and quality/shadow changes.
5. Apply distance haze, restrained bloom, exposure and tone mapping. Composite construction and service overlays, and render the UI after scene processing.

Material responses distinguish grass, asphalt, markings, concrete, brick, plaster, roofs, glass, metal, wood, foliage, rubber, paint and emissive lights. Building generators add recessed windows, trim, doors and roofs. Service buildings include a supported water tank, sewage basins, power stacks, service doors/signage, and a tree-lined park. Physical footprints and simulation behavior remain unchanged.

## Controls and limits

- **High** (default): native primary visibility, half-resolution secondary lighting, two diffuse samples, 100-unit diffuse trace distance, 256-unit reflection distance.
- **Medium**: one diffuse sample, 64-unit diffuse distance and 256-unit reflections, earlier distant detail reduction.
- **Low**: one occlusion sample, 24-unit diffuse distance, 64-unit reflections; shaded surfaces omit the extra diffuse hit-light evaluation.
- `--renderer voxel|legacy`, `--quality low|medium|high`; `--no-shadows` disables sun visibility rays without disabling voxel rendering.
- The build grid starts hidden in voxel mode; it can be enabled from the build panel. Existing overlays, signals, arrows and hover feedback remain available.
- `--camera-path` orbits deterministically for validation. `--no-ui` omits UI from rendering; `--no-grid` hides the construction grid.
- `--capture-sequence folder` writes numbered BMPs after the first 30 frames. Capture waits on the GPU and includes disk I/O: never use sequence captures as performance measurements.

Glass is opaque and reflective. Reflections beyond their trace distance use the sky; lighting is a sampled approximation, not an unlimited path tracer. Fine subpixel details and newly exposed surfaces can still show residual temporal noise. The first version has fixed natural daylight and visual distance haze, with no weather, transparent interiors, destruction, first-person mode or save migration.

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
