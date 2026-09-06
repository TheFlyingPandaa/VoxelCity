# MVP validation — 2026-09-05

## Update: 2×2 road footprints and sidewalks

Road tiles now span 16×16 voxels over four 8×8 grid spaces. The map retains 512×512 road tiles and now spans 8,192×8,192 voxels. Two-voxel sidewalks and one-voxel curbs occupy exposed tile edges.

Debug and Release builds and world tests pass after this update. Added checks cover all four grid-space picks within a road tile, sidewalk width/material, connection openings, sidewalk continuity across chunk seams, and version 1 save migration to version 2. The application painting/erasing test passes with zero DirectX errors. Close-up captures were inspected, and a 1440p overview run renders all 1,024 chunks with zero DirectX errors (9.959 ms average frame time with the debug layer enabled). Updated captures/reports are `artifacts/sidewalks.*`, `artifacts/sidewalk-input.*`, and `artifacts/sidewalk-overview.*`.

The results below describe the original 8×8-voxel road MVP and are retained as historical measurements; they are not benchmarks of the expanded world.

## Original MVP

Validated locally on Windows with an NVIDIA GeForce RTX 4070, MSVC 19.44, and Windows SDK 10.0.26100.0. Both Debug and Release builds compile and pass CTest.

## Functional results

- All 16 cardinal connection masks and their curb openings pass world tests.
- Continuous diagonal strokes remain four-connected; erasing the same stroke restores empty terrain.
- Map bounds, edits on chunk seams, neighboring connection updates, and empty-chunk face merging pass.
- Save/load round trips pass. Truncated data, unsupported versions, and checksum corruption are rejected while preserving the active map.
- The application input test injects mouse events through Dear ImGui: paints seven connected cells, erases the middle cell, and verifies that subsequent clicks over the tool panel do not edit the map. Passed in Debug and Release.
- Automated resize and minimize/restore passed. Final DirectX validation runs produced zero errors and no warning log. An earlier buffer initial-state warning was fixed by explicitly transitioning default-heap buffers from COMMON; its historical log is retained in `artifacts/initial-d3d12-debug.log`.
- Captures were inspected for straight roads, bends, T-junctions, crossroads, dead ends, curbs, lane markings, and the full-map view. All 1,024 chunks fit in the corrected overview.
- Comparing shadowed and unshadowed close-ups outside the interface found 922 sampled pixels darkened by curb shadows. Geometry and camera positions were identical.

## Performance

Release, 2560×1440, VSync off, ray-traced shadows enabled. Static scenarios run for 3,000 frames; the edit stress test runs for 600 frames with the DirectX debug layer enabled. The first 30 frames are excluded from averages.

| Scenario | Average frame | 95th percentile | Average GPU | Video memory |
|---|---:|---:|---:|---:|
| Empty 512×512 map | 0.274 ms | 0.508 ms | 0.212 ms | 135.4 MiB |
| Road network: 31,744 occupied tiles | 0.343 ms | 0.787 ms | 0.290 ms | 198.4 MiB |
| Fully painted: 262,144 occupied tiles | 0.276 ms | 0.522 ms | 0.225 ms | 158.4 MiB |
| Whole road network in view | 0.328 ms | 0.450 ms | 0.124 ms | 198.4 MiB |
| Continuous editing, validation enabled | 3.495 ms | 5.441 ms | 0.584 ms | 199.6 MiB |

These short synthetic runs meet the 16.67 ms frame budget for 60 FPS. They do not predict performance with future buildings, traffic, additional lighting effects, or every possible road arrangement. A completely painted map is geometrically simpler than a sparse road network because interior tiles become connected junction surfaces. Dense-map startup and meshing pauses are excluded from steady-state averages; complete run durations are recorded separately in the JSON reports.

## Reproduce and inspect

Run `scripts/build.ps1` followed by `scripts/verify.ps1`. Reports and matching BMPs are written to `artifacts/`. Key captures are `showcase.bmp`, `curbs.bmp`, `unshadowed.bmp`, `input.bmp`, and `overview.bmp`.

Native file dialogs are wired into the application; persistence logic is covered independently by automated tests. The renderer was tested on this RTX 4070, not on other vendors or unsupported hardware. Primary rendering has no temporal antialiasing, so very distant voxel details may shimmer during camera movement. Chunk meshing is synchronous; large loads can briefly pause the application. No autosave or undo/redo is provided.
