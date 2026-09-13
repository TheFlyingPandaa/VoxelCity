# Visual parity work

Target: Teardown-quality voxel art and rendering within the city game's existing
camera and simulation. Only art and rendering may change; no new packages.
Stationary primary rays must remain fixed at pixel centers.

Reference: https://www.teardowngame.com/ (official screenshot gallery).
The reference is for visual comparison, not an asset source for the game.

## September 6 material and silhouette pass

- Added world-anchored grass and asphalt variation, concrete joints, lower-wall
  weathering, brick color variation, wood grain and slate/terracotta roof colors.
- Matte spatial lighting reconstruction preserves the full-resolution albedo.
  Glossy surfaces retain radiance filtering to avoid tinting reflections.
- Residential roofs now meet at a narrow ridge and include gutters/downpipes.
- Voxel trees and park trees use irregular rounded leaf clusters. The legacy
  renderer keeps its cheaper box canopy geometry. Placement and saves are unchanged.
- No packages, simulation changes or new serialized material IDs.

Comparison captures: `artifacts/art-before-close.bmp` and
`artifacts/art-final-close.bmp`. The latter was inspected after rendering.
Grass variation was reduced after the first capture looked too yellow and blotchy.

Validation: Release build and all eight existing tests passed, including GPU
traversal and vegetation bounds. All nine `verify-voxel.ps1` cases passed with zero
DirectX errors. Metropolis close/overview/district p95: 7.201 / 2.443 / 6.078 ms.
Capture sequences are excluded from performance measurements.

## September 6 architecture pass

The four existing residential variants now alternate roof orientation and include
masonry gables, attic windows, shutters or balconies. Chimneys have caps and flues.
Residential yards add planters, mailboxes and rear fences. Commercial roofs have
parapets and vent slats; storefronts add striped awnings, signs and outdoor tables.
Industrial fronts add shutter slats and loading props. All geometry stays inside
the original parcel, uses existing material IDs and leaves simulation unchanged.

Inspected `artifacts/architecture-close.bmp` and `artifacts/architecture-front.bmp`.
The second capture uses the existing orbit path to expose front elevations rather
than judging new facade details only from the default rear-facing camera.
Release build and all eight tests passed. The close-up capture reported zero
DirectX errors and 6.663 ms p95; it is a short diagnostic, not the city budget gate.
All nine renderer verification cases also passed with zero DirectX errors.
Metropolis close/overview/district p95: 7.230 / 1.632 / 5.862 ms.

## September 6 roof lighting reconstruction pass

Roof lighting previously sampled only the upper-left primary pixel in each 2x2
block, then rejected spatial neighbors on different voxel faces. This isolated
treads and risers and amplified the stepped geometry into coarse lighting bands.

Matte roof reconstruction now gathers neighboring roof faces within a bounded
world-space footprint, preserving instance/material rejection and excluding
opposed normals. High quality integrates all valid roof samples in each fixed 2x2
primary block before downsampling. No frame-dependent primary offsets are added.

Inspected `artifacts/roof-filter-before.bmp`, `artifacts/roof-filter-after.bmp`,
and `artifacts/roof-integrated.bmp`. Integration reduces the coarse sampled
lighting pattern; actual step shading and chimney shadows remain. The integrated
close-up measured 9.464 ms p95 and zero DirectX errors. All eight tests passed.
All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 10.220 / 1.767 / 8.182 ms. A 24-frame stationary
close-up sequence is stored in `artifacts/roof-stationary`; mean temporal standard
deviation across RGB channels was 0.5654 on the 0-255 scale, including moving cars.

## September 6 building shapes and vehicle pass

Residential variant 2 now has a narrower main block, a low side wing, front bay,
side garden and rear deck. Industrial buildings use low halls with three glazed
sawtooth roof sections, loading doors and rear storage props. Both models retain
the existing parcel boundaries and simulation identities.

The shared voxel car adds stepped windscreens, pillars, mirrors, wheel hubs,
grille and number plate. A renderer-only red rear-lens material was appended to
the palette; no save format or traffic behavior changes. Existing GPU traversal
conformance exercises the revised car geometry and material IDs.

Inspected `artifacts/massing-close.bmp`, `artifacts/massing-front.bmp` and
`artifacts/vehicle-detail.bmp`. The two building views expose both the street
fronts and the new rear/side silhouettes. The traffic showcase clearly displays
the windscreens, wheel hubs and red rear lights. Release build and all eight
existing tests passed. No packages were added.
All nine renderer checks passed with zero DirectX errors; metropolis
close/overview/district p95 was 10.477 / 1.680 / 8.418 ms.

## September 6 ground materials pass

Asphalt now has sparse resurfacing patches and subtle cracks. Road paint has
limited wear while retaining continuous markings. Concrete slabs vary slightly
in tone, and grass combines smaller clumps with sparse dry soil. All patterns
use fixed world coordinates and existing material masks; primary rays, road
geometry and simulation are unchanged. No textures or packages were added.

Inspected `artifacts/ground-detail.bmp` and `artifacts/ground-final.bmp`. The
first repair pattern was too frequent and dark, so its density and contrast were
reduced. All eight tests passed. The final 24-frame close-up stationary sequence
in `artifacts/ground-stationary` measured mean RGB temporal standard deviation
0.4833 on the 0-255 scale, including moving traffic.
All nine renderer checks passed with zero DirectX errors; metropolis
close/overview/district p95 was 10.838 / 1.644 / 8.647 ms.

## September 6 glazing and room-depth pass

Vertical building panes now combine Fresnel reflection with a transmitted scene
ray. The voxel traversal can ignore one material, allowing the transmission ray
to pass through glass and stop at actual room geometry. Standard buildings and
the narrow house contain floors, partitions and simple furniture. Factory halls
have hollow interiors beneath thin sawtooth roofs. The primary pane remains the
surface identity; glass history is limited to four samples to reduce parallax trails.

Inspected `artifacts/glazing-close.bmp` and `artifacts/glazing-front.bmp`. Window
brightness and depth now vary with the room behind the pane and the viewing angle.
The first close-up reported 11.749 ms p95 and zero DirectX errors. All eight tests
passed, including new independent CPU checks for ignored homogeneous glass and
glass in front of brick, and paired GPU rays proving transmission reaches the
brick inside the same sparse voxel block.

This is thin-pane transmission without refraction or recursive glass reflections.
Vehicle and horizontal glass keep opaque shading. Interior direct sunlight can
pass through panes, but indirect room lighting still uses an approximate fill.
No packages, simulation changes or save-format changes were made.
All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 11.647 / 1.716 / 9.748 ms. The 24-frame stationary
close-up in `artifacts/glazing-stationary` measured mean RGB temporal standard
deviation 0.4819 on the 0-255 scale, including moving traffic.

## September 6 room illumination and detail-camera pass

Replaced fixed room ambient fill with sampled skylight and one diffuse bounce
through glass. High/Medium/Low use four/two/one samples; Low is occlusion-only.
Stationary glass retains up to 32 samples without current-sample radiance clipping;
pane motion above 0.05 pixels reduces history to four samples. Attic windows now
open into a loft cavity instead of exposing the solid gable behind the pane.

Added optional `--camera-view x y z yaw pitch distance` for reproducible rendering
inspection. It overrides scenario camera defaults, validates finite values and
existing camera limits, and supplies the initial yaw to `--camera-path`. Normal
camera controls and defaults are unchanged.

Inspected `artifacts/room-detail-final.bmp` and the moving detail sequence. The
capture pose is `--camera-view 3864 4 3542 3.35 0.25 22`. Close inspection exposed
the blocked attic panes and excessive room noise that were hard to see from the
district camera. Noise is reduced when stationary but remains visible during
camera motion; the short glass history is still a compromise.

Release build, all eight tests and all nine renderer checks passed with zero
DirectX errors. Metropolis close/overview/district p95: 13.878 / 1.855 / 12.112 ms.
The 24-frame stationary detail sequence measured mean RGB temporal standard
deviation 0.7019 on the 0-255 scale, including moving traffic. No packages added.

## September 7 street scale pass

The detail camera revealed that raised road edges rendered at one world unit,
four times the building foundation height. Both voxel terrain and legacy mesh
terrain now render raised columns at 0.25 units using a shared render scale.
Logical column masks, road connectivity, traffic and saves are unchanged.
Building entry paths and factory aprons connect the frontage to the sidewalk
within existing parcel bounds.

Inspected `artifacts/street-scale-detail.bmp`, `artifacts/street-scale-district.bmp`
and `artifacts/street-scale-final.bmp`. The final detail view has proportionate
street edges and a continuous entrance path. Tests explicitly check quarter-unit
mesh vertices and voxel intersections across road masks and chunk seams. Release
build and all eight tests passed. No packages added.
All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.948 / 1.803 / 11.502 ms.

## September 7 tree structure pass

Rebuilt the four shared voxel tree models with tapered trunks, branching limbs,
surface roots and varied low leaf clumps. Conifers use offset foliage sprays in
place of continuous concentric tiers; broadleaf crowns have uneven cluster heights
and visible gaps. Placement, clearing, saves and the legacy tree meshes are unchanged.
No packages added. Added a parcel-bound check for actual voxel brick bounds,
including the maximum two-unit placement jitter.

Inspected the matching conifer views `artifacts/tree-before.bmp` and
`artifacts/tree-conifer-final.bmp`, plus `artifacts/tree-broadleaf-final.bmp`.
The conifer silhouette is less regular and roots meet the ground more naturally.
The broadleaf crown has more structure, but foliage remains coarse and the four
shared shapes repeat noticeably across the flat landscape. This is not parity.
Release build, all eight tests and all nine renderer checks passed with zero
DirectX errors. Metropolis close/overview/district p95: 13.911 / 1.790 / 11.658 ms.
Captured 24-frame stationary and moving detail sequences in `artifacts/tree-stationary`
and `artifacts/tree-moving`; inspected the final moving frame. Stationary mean RGB
temporal standard deviation was 0.1923 on the 0-255 scale for the conifer view.

## September 7 aerial perspective pass

Replaced uniform distance fog with an analytic exponential height-density integral
in the voxel post pass. Density falls over a 180-unit height scale, preserving
the overview camera while separating distant ground-level silhouettes. The
background and fog share a desaturated horizon color with a subtle sunward glow.
Primary sampling and temporal reconstruction are unchanged. This approximates
unshadowed haze; it does not provide volumetric light shafts or cloud shadows.
Diffuse environment lighting retains its existing sky approximation.

Inspected `artifacts/atmosphere-tree.bmp` against `artifacts/tree-conifer-final.bmp`
and inspected `artifacts/atmosphere-street.bmp`. Distant trees now fade with depth
while nearby foliage and facade detail retain contrast. Release build and all
eight tests passed. No packages added.
All nine renderer checks passed with zero DirectX errors; metropolis
close/overview/district p95: 13.962 / 1.941 / 11.620 ms. Inspected the overview
capture: roads and the city remain visible through the thinner high-altitude haze.
The finite flat map and repeated vegetation remain conspicuous visual limitations.

## September 7 residential facade detail pass

Standard residential models now have projecting stone sills and timber window
bars on all four elevations. The plaster variants also use horizontal sash bars.
Replaced the flat front door with a recessed paneled leaf, glazed upper panels,
transom, metal handle, stone surround and a small emissive entrance lamp using the
existing palette. These details are baked into the shared building models; no
simulation, occupancy, saves, dependencies or packages changed. The separate
narrow-house model retains its existing bay-window and entrance design.

Inspected `artifacts/facade-detail.bmp` against `artifacts/atmosphere-street.bmp`.
Sills now cast readable shadows and the entrance has depth and hardware. Timber
bars remain chunky at the quarter-unit voxel scale; repeated facade layouts and
the coarse roof silhouette still fall short of the reference. Release build and
all eight tests passed. All nine renderer checks passed with zero DirectX errors.
Metropolis close/overview/district p95: 13.631 / 1.823 / 11.350 ms.

## September 7 commercial frontage pass

Fixed large display panes that were placed in front of intact facade masonry.
The commercial frontage now cuts through that masonry and the old small-window
frames, with metal-framed display glass and glazed entrance doors. Added interior
display shelves and goods, handles, stepped sloping striped awnings with hanging
valances and supports, and raised voxel CAFE lettering on a framed sign.
All art uses the existing materials and shared models; no packages added.

Compared `artifacts/shop-before.bmp` with `artifacts/shop-after.bmp` at
`--camera-view 4040 4 3542 3.35 0.25 22`. Displays now have visible depth behind the
glass, and the frontage reads as a shop. The sign crowds the windows above it;
the common upper facade and the single cafe treatment still need architectural
variation. Release build and all eight tests passed, including new paired
visibility/transmission rays through both displays for all four variants.
All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.522 / 1.830 / 11.348 ms.

## September 7 commercial proportions pass

Resolved the cafe sign overlap by giving shops a taller ground storey, moving
upper windows above the fascia and lowering the sign a quarter unit. Interior
floor spacing now follows the taller ground storey. Residential and service
interiors retain their existing spacing. Even commercial variants use brick and
three upper windows; odd variants use the pale commercial finish and two wider
metal-divided windows on each elevation. Display glazing and shelving are retained.

Inspected `artifacts/shop-proportions.bmp` and `artifacts/shop-variant.bmp`, covering
the taller and shorter brick buildings. Signs now sit below the upper panes.
Release build and all eight tests passed, including display transmission and model
bounds. No packages or simulation changes. Inspected the pale wide-window version
in `artifacts/shop-wide-check.bmp` at `--camera-view 4040 4 3590 3.35 0.25 22`.
All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.505 / 1.763 / 11.330 ms.

## September 7 narrow-house glazing pass

Fixed the separate narrow-house model's panes on all main elevations so they
replace the full wall thickness. Hollowed the side wing, connected it to the main
room, and fitted framed front and side glazing. Opened the projecting bay into
the living room and added side panes and a low timber window seat. The bay and
wing had previously retained solid material directly behind their glass.

Inspected `artifacts/narrow-house-glazing.bmp` at
`--camera-view 4024 4 3590 3.35 0.25 22`. The bay and wing now show interior depth.
Regression rays cover all main elevations, the bay and both wing faces at levels
one and two. They require first-hit glass and no immediate opaque obstruction;
exiting through an opposite window is valid. Release build and all eight tests
passed. No packages or simulation changes. Inspected the unobstructed oblique
view `artifacts/narrow-wing-final.bmp` at `--camera-view 4027 3 3593 2.7 0.3 22`.
All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.580 / 1.805 / 11.271 ms.

## September 7 narrow-house roof construction pass

Replaced the narrow-house solid roof-colored wedge with thin roof courses over
masonry gables, an open central loft and a timber loft floor. Added attic panes
on both ends, gutters, a downpipe and a dark chimney flue using existing materials.
Extended the glazing regression rays to the attic at both building levels.

Compared `artifacts/narrow-roof-front.bmp` and `artifacts/narrow-roof-side.bmp`
against the preceding narrow-house captures. The gable now reads as part of the
brick building beneath an overhanging roof, and the attic pane shows depth. Roof
steps remain visible at this close distance; the change does not solve the broader
fine-geometry gap. No packages or simulation changes. Release build, all eight
tests and all nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.569 / 1.851 / 11.285 ms.

## September 7 industrial service detail pass

Added side-wall piers, a louvered vent, roof-access ladder, supported pipework,
front bollards, a painted glazed personnel door, pallet slats, crate bands and a
rear waste container. Roller-door ribs now use metal instead of black rubber,
so the loading doors read as continuous corrugated metal. All fittings stay in
the factory parcel and use existing shared models and materials.

Compared `artifacts/factory-before.bmp` with `artifacts/factory-front-detail.bmp`
and inspected `artifacts/factory-yard-detail.bmp`. The factory has clearer scale
and service details; the rear wall remains plain and industrial variants still
share the same hall silhouette. Extended the ladder above the raised roof edge
after the first visual check. Inspected the corrected `artifacts/factory-final.bmp`.
No packages or simulation changes. Final Release build and all eight tests passed.
All nine final renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.817 / 1.802 / 11.521 ms.

## September 7 masonry definition pass

Added staggered mortar beds to the brick material and aligned brick-to-brick color
variation to the continuous bond coordinates. Primary shading estimates the world
space pixel footprint from depth, the fixed camera FOV and surface incidence;
unresolved joints and brick contrast blend toward their area average. The pattern
is world-anchored and adds no temporal jitter or geometry. Secondary rays retain
point-sampled material evaluation.

Inspected `artifacts/mortar-detail.bmp` against `artifacts/facade-detail.bmp`.
Brick courses now read separately from the larger voxel color variation. This
adds surface definition but does not provide geometric mortar recesses or fix
the coarse silhouettes. Release build and all eight tests passed. No packages.
All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.872 / 1.749 / 11.705 ms. Captured 24-frame stationary
and moving detail sequences in `artifacts/mortar-stationary` and
`artifacts/mortar-moving`; inspected the final moving frame. Stationary mean RGB
temporal standard deviation was 0.6712 on the 0-255 scale, including lighting noise
and moving traffic. This aggregate does not isolate mortar shimmer during motion.

## September 7 residential lot edge pass

Replaced standard residential rear fence slabs with individual pickets, horizontal
rails and capped posts. Side hedges now use overlapping foliage clusters rather
than solid bars, and compact wheeled bins sit beside the house. The separate narrow
house retains its garden and deck layout. Foliage baking now visits only the
cluster's bounding cells while retaining the original center-in-bounds test and
voxel noise; this limits the cost of adding hedge clusters.

Compared `artifacts/garden-before.bmp` with `artifacts/garden-after.bmp` at
`--camera-view 3864 3 3548 0.4 0.4 22`. Fence gaps reveal the lot behind them and
hedges have less regular edges. These remain repeated shared parcel details, not
fully authored gardens. Release build and all eight tests passed. No packages or
simulation changes. All nine renderer checks passed with zero DirectX errors.
Metropolis close/overview/district p95: 13.988 / 1.931 / 11.591 ms.

## September 7 water-tower silhouette pass

Replaced the box-shaped reservoir with a stepped round tank and conical roof.
Added metal tank bands, concrete footings, crossed support braces, a circular
catwalk with railings, an access ladder and a narrower central supply pipe.
The reservoir remains the existing water-service building with the same parcel
and simulation behavior; only its voxel model changed. No packages added.

Compared `artifacts/water-tower-before.bmp` with `artifacts/water-tower-after.bmp`
at `--camera-view 3880 6 3512 3.35 0.3 26`. The tower now has a distinct service
silhouette and visible construction. Rounded surfaces and diagonal braces still
show the coarse voxel steps at close range. Release build and all eight tests
passed, including model bounds and GPU traversal. All nine renderer checks passed
with zero DirectX errors. Metropolis close/overview/district p95:
13.928 / 1.807 / 11.580 ms.

## September 7 treatment-basin pass

Recessed the treatment plant's two basins below concrete rim walls and added a
railed service bridge, central steps, inlet pipes and a control cabinet. Added
VWater at the end of the existing renderer palette: murky green diffuse color,
low roughness and subtle world-anchored shading-normal ripples. Primary geometry
and edges remain stationary. Water is opaque reflective shading, without
refraction, animated flow or volumetric absorption. No simulation or save changes.

Inspected `artifacts/treatment-basins.bmp` at
`--camera-view 3896 2 3512 3.35 0.65 22`, then connected the inlet pipe elbows after
the visual check. Regression rays verify water at height 1.25 and rims at 1.75.
No packages added. Final Release build, all eight tests and all nine renderer
checks passed with zero DirectX errors. Metropolis close/overview/district p95:
13.820 / 1.899 / 11.702 ms. Inspected the final frame of the 24-frame sequence in
`artifacts/treatment-stationary`; mean RGB temporal standard deviation was 0.7531
on the 0-255 scale, including surrounding traffic and lighting noise.

## September 7 fire-station identity pass

Replaced the generic fire-service office with a low apparatus hall, rear office
volume and narrow hose tower. Added two segmented red glazed doors, a separate
glazed pedestrian entrance, flush apron markings and raised FIRE lettering.
VRedPaint is appended to the renderer palette so station doors retain a consistent
red finish instead of inheriting the random vehicle-paint palette. No packages,
simulation changes or save-format changes.

Compared `artifacts/fire-station-before.bmp` and `artifacts/fire-station-after.bmp`.
The new silhouette and frontage identify the service without a UI label. Corrected
raised apron strips to flush markings after the first render. Inspected the final
`artifacts/fire-station-final.bmp` at `--camera-view 3944 4 3512 3.35 0.35 26`.
The hall remains sparsely furnished and all fire stations share this model.
Final Release build and all eight tests passed. All nine renderer checks passed
with zero DirectX errors. Metropolis close/overview/district p95:
13.772 / 1.969 / 11.752 ms.

## September 7 clinic layout pass

Replaced the generic clinic office with a narrower treatment block and a lower
glazed reception wing, connected internally. Added a covered approach, medical
sign, entrance hardware, bench and roof ventilation. The cross now sits above the
actual entrance rather than covering upper windows. Uses existing materials and
the original parcel; no packages, simulation or save changes.

Compared `artifacts/clinic-before.bmp` with `artifacts/clinic-after.bmp` at
`--camera-view 3928 4 3512 3.35 0.35 26`. The lower wing and covered entrance give
the clinic a separate layout from the nearby office and fire station. The interior
still uses generic furniture, and the treatment block remains visually simple.
Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
13.576 / 1.870 / 11.517 ms.

## September 7 finer vehicle voxels

Sparse CPU and GPU traversal now derives cell dimensions from each primitive's
bounds. Cars use eighth-unit cells for rounded tire profiles, smaller hubs and
thin door handles; buildings and trees retain quarter-unit cells. The packed
8-cubed brick format is unchanged, and no packages were added. Primary camera
rays still use fixed pixel centers.

Inspected stationary and moving captures in `artifacts/fine-car-stationary` and
`artifacts/fine-car-moving`, using showcase with 200 cars, seed 42, warmup 600,
54 frames and `--camera-view 4136 1 4152 0.65 0.35 22`. Tires have a rounder
silhouette, but the shared car body and opaque cabin remain visibly simple.
These traffic captures are visual checks, not a measurement of stationary noise.

Release build and all eight tests passed, including an independent exhaustive
reference with mixed brick sizes and CPU/GPU traversal conformance. All nine
renderer checks passed with zero DirectX errors before the final handle cleanup;
metropolis close/overview/district p95 was 13.916 / 1.841 / 11.805 ms. The final
handle cleanup was rebuilt and passed all eight tests and both visual captures.

## September 7 vehicle cabin pass

Replaced solid glass car cabins with thin stepped windscreens, side panes,
painted pillars and a thinner roof around an open interior. Added front seats
with headrests, a rear bench, dashboard and steering hardware. Vehicle glass
now uses the existing transmission/reflection shading, including horizontal
faces on the stepped windscreens. Moving glass retains the short history limit.
No packages or simulation changes; fixed primary pixel sampling is preserved.

Compared `artifacts/fine-car-stationary/frame-00023.bmp` with
`artifacts/car-cabin-final/frame-00023.bmp` and inspected the matching moving
capture in `artifacts/car-cabin-moving`. Same showcase pose, seed and warmup as
the previous vehicle pass. Seats now give the windows visible depth. Windshield
steps still produce faceted reflections, and all vehicles share one silhouette;
neither vehicle realism nor overall Teardown parity is complete.

Release build and all eight tests passed. New windscreen regression rays verify
glass followed by cabin furnishings, in addition to exhaustive traversal and
CPU/GPU conformance. All nine final renderer checks passed with zero DirectX
errors. Metropolis close/overview/district p95: 14.067 / 2.189 / 11.948 ms,
within the 16.67 ms budget.

## September 7 asphalt detail pass

Replaced the asphalt's coarse quarter-unit base noise with lower-contrast fine
grain that fades with projected pixel footprint. Existing repairs and cracks
remain. Added occasional flush iron utility covers with filtered rims and ribs,
clipped to asphalt. Covers are a material treatment, with no extra geometry,
packages, terrain height changes or simulation changes.

Inspected `artifacts/asphalt-final/frame-00023.bmp` and the matching moving
capture in `artifacts/asphalt-moving`. Showcase, zero cars, 54 frames, seed 42,
`--camera-view 4165 0 4149 0.65 0.8 18`. Reduced the first grain attempt after
the close view showed excessive contrast. Final 24-frame stationary sequence
has mean per-channel temporal standard deviation 0.111 on the 0-255 scale;
the cover crop is 0.083. This measures this static scene only, not general
lighting stability. Primary sampling remains fixed at pixel centers.

The road reads less like a coarse checker, and the cover adds a recognizable
street detail. Grass, curbs and paint remain visibly simple. Overall parity is
still incomplete. Release build and all eight tests passed. All nine renderer
checks passed with zero DirectX errors. Metropolis close/overview/district p95:
14.289 / 2.472 / 12.064 ms, within the 16.67 ms budget.

## September 7 climbing ivy pass

Compared the current town close view with the reference: vegetation layered
against architecture is still missing across most plots. Added a rooted climber
to residential variant zero, with woody stems and leaf clusters wrapping the
front corner and spreading between window rows. Geometry remains within the
parcel. No packages, simulation or camera sampling changes.

Inspected `artifacts/ivy-final/frame-00023.bmp` against the earlier
`artifacts/mortar-detail.bmp` at `--camera-view 3864 4 3542 3.35 0.25 22`.
Also inspected `artifacts/ivy-final-moving/frame-00023.bmp`. The first attempt
was too thick and regular; the final version exposes stems and more masonry,
with shorter tapered branches. Windows remain readable and the leaves cast
real shadows. The shared climbing pattern is still evident on neighboring
houses, and its leaves remain coarse compared with the reference.

Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
14.351 / 1.942 / 11.899 ms, within the 16.67 ms budget.
Overall Teardown parity is incomplete.

## September 7 foliage backlighting pass

Leaf materials now admit restrained sunlight from behind a thin cluster.
The shader checks opaque blockers within the first 0.75 world units, then all
occluders beyond that span, so walls and thicker canopies still block the light.
Applied to direct shading and diffuse bounce evaluation, using the existing
ray tracer. This is a bounded approximation, not volumetric scattering. No
packages, geometry or primary camera sampling changes.

Compared `artifacts/foliage-backlight/frame-00023.bmp` with the earlier
`artifacts/atmosphere-tree.bmp`, using the same tree pose
`--camera-view 3064.5 4 3049.75 0.65 0.3 22`. Thin outer clusters are less dark;
the canopy interior retains depth. The coarse clustered tree shape remains a
larger gap relative to the reference. Also inspected the matching moving capture
in `artifacts/foliage-backlight-moving`. Release build and all eight tests passed.
All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 15.418 / 2.089 / 13.102 ms. Close-view headroom is
now about 1.25 ms against the 16.67 ms budget; further rendering work needs to
account for this added cost. Overall Teardown parity is still incomplete.

## September 7 conifer branch sprays

Replaced the conifers' bulky foliage lobes with thinner overlapping sprays along
tapered twig fans. Narrowed the central foliage and retained a leafy leader.
Visible gaps now reveal branches and separate canopy layers. Both conifer
variants keep the existing placement, parcel bounds and trunk. No packages,
simulation changes or camera sampling changes.

Compared `artifacts/conifer-sprays/frame-00023.bmp` against
`artifacts/foliage-backlight/frame-00023.bmp`, using the same tree pose and seed.
Inspected the matching moving view in `artifacts/conifer-sprays-moving` as well.
The canopy is more open and less bulbous, though the repeated tiers and coarse
leaf cells remain visible. Overall Teardown parity is not achieved.

Release build and all eight tests passed, including tree bounds, trunk/leaf
intersections and CPU/GPU traversal conformance. All nine renderer checks passed
with zero DirectX errors. Metropolis close/overview/district p95:
15.150 / 2.050 / 12.992 ms, within the 16.67 ms budget.

## September 7 school layout pass

Replaced the generic service office used by the school with an L-shaped plan:
a low brick classroom wing and a taller entrance block around a paved courtyard.
Added classroom desks, chairs and teaching boards behind transmitting windows,
a covered glazed entrance, clock, bench and flush play markings. The model stays
within its original parcel and uses existing materials. No packages or simulation
changes; stationary primary sampling is preserved.

Compared `artifacts/school-before.bmp` with
`artifacts/school-final/frame-00023.bmp`, at
`--camera-view 4136 3 3512 3.35 0.45 26`, town with seed 42. Also inspected
`artifacts/school-moving/frame-00023.bmp`. The lower classroom wing and courtyard
give the school its own silhouette. Roofs and side walls remain simple, and the
shared model does not yet offer campus variations. Overall parity is incomplete.

Release build and all eight tests passed, including new classroom rays that
verify glass followed by teaching boards across the room. All nine renderer
checks passed with zero DirectX errors. Metropolis close/overview/district p95:
15.148 / 2.116 / 12.883 ms, within the 16.67 ms budget.

## September 7 street fixture pass

Added occasional sidewalk lamp posts to ordinary road parcels, with concrete
bases, metal poles, stepped arms and recessed diffuser faces. Placement checks
the existing sidewalk material and skips highways. Fixtures are part of each
road chunk's rendered model, so road rebuilding removes them. All geometry stays
inside the road parcel. No packages or simulation changes, and no new nighttime
lighting system; these are daytime fixtures with normal shadows and reflections.

Inspected `artifacts/street-fixtures/frame-00023.bmp` at
`--camera-view 4152.5 2 4145 0.65 0.35 22`, showcase, 200 cars, warmup 600,
seed 42. They add a vertical street element and scale beside the vehicles.
The fixtures share one design and distant thin edges still lack antialiasing.
Overall Teardown parity remains incomplete.

Release build and all eight tests passed. Added and ran a regression checking
fixture generation, chunk bounds and removal when the supporting road is erased.
Also inspected `artifacts/street-fixtures-moving/frame-00023.bmp`. All nine
renderer checks passed with zero DirectX errors. Metropolis close/overview/district
p95: 15.306 / 2.103 / 12.899 ms, within the 16.67 ms budget.

## September 7 windscreen reflection pass

The street-fixture captures exposed strong horizontal bands on the cars' stepped
windscreens. Primary shading now uses the overall 45-degree windscreen normal,
transformed with the car pose, for Fresnel/reflection evaluation. Side panes retain
their original normals. Ray intersections, voxel outlines and fixed primary
sampling are unchanged. This rule follows the current car model's pane bounds
and must be updated if that geometry changes.

Compared `artifacts/windscreen-slope/frame-00023.bmp` with the matching
`artifacts/street-fixtures/frame-00023.bmp`, then inspected
`artifacts/windscreen-slope-moving/frame-00023.bmp`. The horizontal bands are
gone and the windscreen reads as continuous glass. Approximate reflection
lighting and the single shared vehicle shape remain limitations. No packages.
Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
15.225 / 2.120 / 12.922 ms, within the 16.67 ms budget.
Teardown parity remains incomplete.

## September 7 broadleaf crown pass

Split the broadleaf models' large rounded foliage masses into smaller offset
crowns on forked branches, including a divided upper crown. The trunks, roots,
placement and parcel constraints remain unchanged. No packages or simulation
changes; fixed camera sampling and existing foliage lighting are preserved.

Compared `artifacts/broadleaf-before.bmp` with
`artifacts/broadleaf-crowns/frame-00023.bmp` at
`--camera-view 2922 5 3030.75 0.65 0.3 22`. Also inspected the matching moving
view in `artifacts/broadleaf-crowns-moving`. Branches are more visible and the
shadow has a less uniform outline. Leaf clusters still look coarse and shared
tree shapes remain recognizable; overall Teardown parity is incomplete.

Release build and all eight tests passed, including tree bounds, trunk/leaf
intersections and traversal conformance. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
15.206 / 2.265 / 12.947 ms, within the 16.67 ms budget.

## September 7 foliage traversal consolidation

Backlighting now visits the scene once and walks each candidate brick once,
ignoring leaves only until 0.75 world units along the shadow ray. Other materials
block throughout. This also closes the small bias gap between the previous two
ray intervals. Normal glass-skip and opaque traversal retain their existing
behavior. No packages or art changes; primary sampling is unchanged.

Expanded GPU conformance to compare the distance-limited leaf rule against two
CPU reference intersections across randomized car/tree bricks. Release build and
all eight tests passed. Compared fresh matched captures in
`artifacts/backlight-two-query` and `artifacts/backlight-single-walk`: final-frame
mean absolute RGB difference is 0.000279 on the 0-255 scale, maximum 10, across
548 changed pixels. Changes are localized; the displayed foliage lighting remains
visually consistent. A first version with two brick walks showed no useful gain
and was replaced by the distance-limited walk. All nine renderer checks passed
with zero DirectX errors. Metropolis close/overview/district p95:
14.948 / 2.048 / 12.860 ms. Close-view average GPU time is effectively unchanged
at 14.130 ms versus the preceding 14.121 ms, so a speedup is not established.
Reported peak video memory fell from 653.789 to 634.039 MiB (19.75 MiB).
Kept the consolidated traversal for that memory reduction and the tested single
distance rule. Teardown visual parity remains incomplete.

## September 7 timber and bark materials

Separated tree bark from finished timber. Tree limbs now use a dedicated bark
palette entry with darker longitudinal variation and a dampened base. Doors,
shutters, fences and furniture retain timber, with finer elongated grain and
subtle board seams. Fine patterns fade by projected pixel footprint. No packages,
geometry, simulation or primary sampling changes; material IDs are procedural.

Inspected `artifacts/timber-detail/frame-00023.bmp`, the matching
`artifacts/timber-moving/frame-00023.bmp`, and
`artifacts/bark-detail/frame-00023.bmp`. The grain reads more clearly on shutters
and doors, while the trunk is less orange and no longer shares board seams.
Coarse geometry and the generic directional grain remain limitations. Overall
Teardown parity is incomplete.

Release build and all eight tests passed. Tree material assertions now check
bark, while geometry/bounds and CPU/GPU traversal checks remain in place.
All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 15.341 / 2.013 / 12.978 ms,
within the 16.67 ms budget.

## September 7 metal finishes

Static metal now has subtle fixed staining, fine filtered surface variation and
small oxidized areas, stronger near ground level. Oxidation also changes roughness
and metal response. Vehicle hardware uses a separate polished-metal palette entry,
avoiding world-anchored wear moving across the cars. No packages, geometry or
simulation changes; camera sampling remains fixed.

Compared matching `artifacts/metal-before/frame-00023.bmp` and
`artifacts/metal-weathering/frame-00023.bmp`, at
`--camera-view 4008 3 3590 2.7 0.3 22`, town with seed 42. Also inspected
`artifacts/metal-weathering-moving/frame-00023.bmp`. The effect is restrained:
less uniform metal color while retaining readable door ribs and small hardware.
The factory still needs more authored surface damage and varied equipment.
Overall Teardown parity is incomplete. Release build and all eight tests passed.
All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 15.423 / 1.993 / 13.145 ms,
within the 16.67 ms budget.

## September 7 daylight direction pass

Moved the voxel renderer's sun around to illuminate the street-facing facades
and slightly warmed its direct radiance. Sun elevation, sky fill and tone mapping
are unchanged. The new direction makes the existing roof overhangs, sills and
door surrounds cast visible facade shadows instead of leaving the primary close
views almost entirely in shade. No packages, geometry, simulation or primary
sampling changes.

Compared `artifacts/lighting-factory/frame-00023.bmp` with
`artifacts/metal-weathering/frame-00023.bmp` at the same factory pose. The front
wall reads with more depth and the brickwork is warmer; pale concrete retains
detail. This lighting pass exposes the coarse surface patterns and repeated
building models more clearly, so those remain gaps. Also inspected
`artifacts/lighting-after-wide.bmp` and the matching factory moving capture in
`artifacts/lighting-factory-moving`. Teardown parity is incomplete.
Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
15.488 / 2.090 / 13.109 ms, within the 16.67 ms budget.

## September 7 industrial concrete panels

Replaced the industrial wall material's coarse per-voxel noise with restrained
panel variation, fine grain and narrow construction joints. Fine details fade
with pixel footprint, and lower walls receive subtle staining. This is a material
treatment with no new geometry, packages, simulation or primary sampling changes.

Compared `artifacts/concrete-panels/frame-00023.bmp` with the matching
`artifacts/lighting-factory/frame-00023.bmp`, and inspected
`artifacts/concrete-panels-moving/frame-00023.bmp`. Concrete reads more clearly
as panels without competing with the roof and door shadows. The factory still
needs more varied equipment and authored damage; Teardown parity is incomplete.
Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
15.697 / 2.328 / 13.339 ms, within the 16.67 ms budget.

## September 7 factory storage drums

Added two chamfered storage drums on wooden pads in the factory's spare rear
corner. Metal hoops, contrasting lid paint and filler caps add recognizable
workshop equipment alongside the existing crates and waste bin. They stay clear
of the side ladder and use existing materials; no packages or renderer changes.

Compared `artifacts/yard-before/frame-00023.bmp` with
`artifacts/yard-drums/frame-00023.bmp` at
`--camera-view 4008 3 3595 .4 .45 22`, town with seed 42, and inspected
`artifacts/yard-drums-moving/frame-00023.bmp`. The storage corner reads more
clearly, although the drum silhouettes remain coarse and repeated across factories.
Fixed primary sampling is preserved. Fine lighting noise remains: the last eight
stationary frames have mean per-channel temporal standard deviation 0.531 on the
0-255 scale; this is not a claim of an entirely motionless image.
Release build and all eight tests passed. Metropolis close/overview/district p95:
15.434 / 2.152 / 12.884 ms, within the 16.67 ms budget.
All nine renderer checks passed with zero DirectX errors.
Teardown parity remains incomplete.

## September 7 industrial sheet roofing

Factory sawtooth roofs now use a dedicated weathered sheet-metal material instead
of the residential roof palette. Muted green-gray metal, individual sheet tones,
filtered seams and restrained seam oxidation distinguish the industrial halls from
the nearby brick houses. The material participates in the existing roof footprint,
spatial and temporal reconstruction. Primary samples remain fixed; no new packages.

Compared `artifacts/yard-drums/frame-00023.bmp` with
`artifacts/sheet-roof-final/frame-00023.bmp`, and inspected the matching camera-path
capture in `artifacts/sheet-roof-final-moving`. Both use the rear factory view
`--camera-view 4008 3 3595 .4 .45 22`, town with seed 42.
The material distinction is clearer, but the coarse roof steps and repeated hall
shapes remain visible. Teardown parity is incomplete. The last eight stationary
frames have mean per-channel temporal standard deviation 0.5307 on the 0-255
scale, essentially unchanged from the preceding pass's 0.531; lighting noise remains.
Release build and all eight tests passed; GPU conformance passed again after
simplifying the weathering calculation. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
16.479 / 2.045 / 13.433 ms. Close-view headroom against 16.67 ms is narrow,
and reported peak video memory rose from 634.227 to 664.289 MiB versus the
preceding pass; further shader work should address that cost.

## September 7 primary material reuse

Lighting and thin-surface fallback now reconstruct material response from the
stored primary color/roughness sample instead of repeating procedural material
evaluation. Metal oxidation and asphalt cover metalness are recovered from their
existing linear roughness mappings. This keeps metal response consistent with
footprint-filtered color. Secondary ray hits still evaluate their own materials.
Future changes to those roughness mappings must update `surfaceMaterial` too.
No geometry, packages or camera sampling changes.

Compared `artifacts/sheet-roof-final/frame-00023.bmp` with
`artifacts/surface-reuse/frame-00023.bmp`, and inspected
`artifacts/surface-reuse-moving/frame-00023.bmp`. The rear factory view retains
its appearance: mean absolute RGB difference 0.00748 on the 0-255 scale, maximum
8. Small differences are expected from filtered metal response and half-float
roughness storage. This comparison does not prove equivalence for every material.

Shader build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
16.360 / 2.163 / 13.389 ms; close average GPU time 15.080 ms and peak video memory
664.227 MiB. The small timing difference from the prior pass is not a demonstrated
large performance gain; headroom remains narrow. Teardown parity remains incomplete.

## September 7 finer factory roof assembly

Factory sawtooth shells, gable ends and raised glazing now use eighth-unit cells.
Roof thickness and riser height are halved, and window mullions are thinner.
Walls and yard props retain quarter-unit cells. A separate roof canvas is merged
into the building with corrected packed-data offsets and vertical bounds; the
chimney opening excludes overlapping roof cells. No packages or shading changes.

Compared `artifacts/surface-reuse/frame-00023.bmp` with
`artifacts/fine-factory-roof/frame-00023.bmp` and inspected
`artifacts/fine-factory-roof-moving/frame-00023.bmp`. The roof edge and glazing
read less heavily, with smaller stair steps. Steps remain visible at close range,
and this pass does not refine the residential roofs. Teardown parity is incomplete.

Release build and all eight tests passed. Added a downward-ray coverage check
across every roof strip at two building levels to validate merged brick offsets,
roof material and surface height; VoxelTests passed again. All nine renderer checks
passed with zero DirectX errors. Metropolis close/overview/district p95:
16.093 / 2.105 / 13.256 ms, within 16.67 ms. Close peak video memory is
667.367 MiB versus 664.227 MiB before; the timing difference is not proof that
finer geometry is faster. Fixed primary camera sampling is preserved.

## September 7 finer main residential roofs

Main residential variants 0, 1 and 3 now have eighth-unit slope steps and thinner
roof shells/ridge caps. Gables, attic openings and roof fittings share the fine
assembly, with the downpipe retained in the wall canvas. The narrow side-wing
variant remains unchanged. Cells stay quarter-unit along the ridge, and bricks
with exactly constant, matching ridge profiles merge into longer primitives.
This preserves occupied volume while reducing separate traversal candidates.

The first fine-roof build exceeded the close-view budget (17.145 ms p95).
Anisotropic cells and repeated-brick merging reduced that to 16.709 ms. Roof
lighting now uses one evaluation when all four primary samples belong to the
same planar face; blocks crossing roof steps or boundaries retain the existing
four-sample integration. Fixed primary rays and temporal roof history remain.
No packages were added.

Compared `artifacts/res-roof-before/frame-00023.bmp` with
`artifacts/res-roof-final/frame-00023.bmp`, and inspected
`artifacts/res-roof-final-moving/frame-00023.bmp`, at
`--camera-view 3864 4 3542 3.35 .25 22`, town with seed 42. Gable edges and nearby
roof slopes are visibly finer. Whole-image mean temporal standard deviation over
the last eight still frames is 0.3046 versus 0.3006 before, on a 0-255 scale;
fine lighting noise remains and is slightly higher in this comparison.

All eight tests passed, including added roof coverage checks for both ridge
orientations. GPU conformance passed again after the shader change. All nine
renderer checks passed with zero DirectX errors. Final metropolis
close/overview/district p95: 16.233 / 2.090 / 13.770 ms, within 16.67 ms.
Close peak video memory: 667.438 MiB. Teardown parity remains incomplete;
window surrounds, foliage and the narrow house roof still look coarse.

## September 7 residential roof surface pattern

Replaced the roof palette's independent voxel-face noise with restrained tile
variation and narrow, staggered joints. The world-XZ projected pattern fades to
its area average with pixel footprint. It is a surface treatment, not individual
tile geometry or roof-oriented UV mapping. No packages or primary sampling changes.

Compared `artifacts/roof-surface-before/frame-00023.bmp` with
`artifacts/roof-tiles/frame-00023.bmp`, and inspected
`artifacts/roof-tiles-moving/frame-00023.bmp`, at the higher view
`--camera-view 3864 6 3544 2.5 .45 23`, town with seed 42. The gray roof shows
clearer tile structure with less checker-like color mottling. This angle also
exposes strong alternating tread/riser lighting on the brown roof. That remains
an unresolved rendering issue and should be addressed before more roof detail.
Teardown parity is incomplete.
Shader build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
16.247 / 2.148 / 13.584 ms, within the 16.67 ms budget.

## September 7 residential roof slope lighting

Main residential models now provide renderer-only ridge orientation and eave
height in the instance record (64 bytes, matching CPU and HLSL). Roof top/tread
faces use the authored 0.75 slope for primary shading; end faces and undersides
retain their geometric normals. Primary hit positions and voxel silhouettes stay
exact. Lighting origins lift vertically to the staircase's upper envelope, bounded
at 0.25 units and stored in the previously unused motion.w component. Lighting
and thin-surface fallback use that lift, and planar-footprint detection uses the
same surface. This is a shading approximation specific to the current main
residential roof geometry; it must change if that geometry changes.

Compared `artifacts/roof-tiles/frame-00023.bmp` with
`artifacts/roof-slope-light/frame-00023.bmp`, and inspected
`artifacts/roof-slope-light-moving/frame-00023.bmp`. The strong tread/riser bands
are removed from the visible brown roof, while the chimney casts a connected
shadow and the voxel silhouette remains visible. Mean per-channel temporal
standard deviation over the last eight still frames is 0.3545 versus 0.3619
before (0-255 scale); fine lighting noise remains. The lift can shift very small
contact shadows. Secondary-ray roof shading, factory roofs and the narrow house
variant still use their existing normals; this is not universal slope shading.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.617 / 2.189 / 12.660 ms, within 16.67 ms.
The close-view improvement accompanies more planar roof blocks using a single
lighting evaluation. No packages added. Teardown parity remains incomplete.
Release shader/application build and all eight tests, including GPU conformance,
passed with the updated instance layout.

## September 7 factory slope lighting

Factory instances now carry their roof origin and eave height. Primary sheet-roof
shading uses the authored 0.5 pitch over each four-unit sawtooth run, with a
vertical lighting-origin lift bounded at 0.125 units. Top faces and rising steps
use the slope normal; exposed high ends, gables, glazing and undersides retain
their existing response. Primary intersections remain exact. No packages added.

Compared `artifacts/factory-slope-before/frame-00023.bmp` with
`artifacts/factory-slope-light/frame-00023.bmp`, and inspected
`artifacts/factory-slope-light-moving/frame-00023.bmp`. The roof planes read more
evenly while glazing, chimney shadows and the stepped edge remain visible. The
effect is subtler than on the residential roof. Last-eight-frame mean temporal
standard deviation is 0.5314 versus 0.5309 before, on the 0-255 scale: fine
lighting noise remains effectively unchanged in this view. Secondary-ray roof
shading and the narrow house still need separate treatment. Teardown parity
remains incomplete.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.717 / 2.260 / 12.100 ms, within 16.67 ms. Close average GPU time is 11.727 ms.
Planar factory roof blocks can now use the existing single-evaluation path.

## September 7 narrow-house slope lighting

Roof metadata now includes span, pitch, envelope offset and maximum lift in an
80-byte CPU/GPU instance record. The narrow residential variant uses its actual
four-unit half-span, 0.75 pitch and quarter-unit step envelope. Slope shading is
restricted to hits above the main eaves, preserving the side-wing and bay's flat
roof response. Its lighting-origin lift is bounded at 0.5 units; the other roof
profiles retain their smaller bounds. This approximation can shift small contact
shadows and does not change the coarse narrow-roof silhouette. No packages added.

Compared `artifacts/narrow-slope-before/frame-00023.bmp` with
`artifacts/narrow-slope-light/frame-00023.bmp`, and inspected
`artifacts/narrow-slope-light-moving/frame-00023.bmp`, at
`--camera-view 4024 5 3590 2.7 .4 24`, town with seed 42. Strong roof bands are
removed while the chimney shadow and flat low roofs remain readable. Last-eight-
frame mean temporal standard deviation is 0.4539 versus 0.4565 before, on the
0-255 scale; fine lighting noise remains. Secondary-ray roof shading still uses
geometric normals. Teardown parity remains incomplete.

Release build passed. All nine renderer checks passed with zero DirectX errors.
Metropolis close/overview/district p95: 12.456 / 2.187 / 12.187 ms,
within 16.67 ms.
GPU conformance passed with the updated instance layout.

## September 7 framed residential shutters

Replaced variant-0 townhouse shutter slabs with eighth-unit timber frames,
recessed panels, horizontal louvers and hinge blocks on street/garden facades.
Separate fine canvases merge into the existing model. Existing facade trim and
ivy clip the new geometry, preventing overlapping surfaces; covered shutter
supports also suppress their hinge blocks. No packages or camera sampling changes.

Compared `artifacts/shutters-before/frame-00023.bmp` with
`artifacts/shutters-clean/frame-00023.bmp`, and inspected
`artifacts/shutters-clean-moving/frame-00023.bmp`. Insets now cast readable
small shadows. An initial overlap artifact at ivy/entrance trim was corrected
before the final captures. The shutters remain chunky at this scale, and repeated
facade layouts are still obvious. Teardown parity remains incomplete.

Release build and all eight tests passed after clipping overlapping surfaces.
All nine renderer checks then passed with zero DirectX errors; metropolis
close/overview/district p95: 12.336 / 2.285 / 12.101 ms. The final cleanup only
removes covered hinge cells; it was rebuilt, passed VoxelTests and was checked in
the final still/moving captures. The full benchmark was not repeated for that
small removal.

## September 7 coherent leaf color

Leaf material now uses continuous cluster-scale color variation and restrained
per-voxel mottling that fades with pixel footprint. Removed the previous two
layers of high-contrast voxel noise. Species palette, leaf transmission, branch
geometry and fixed primary camera rays are unchanged; no packages added.

Compared `artifacts/leaf-color-before/frame-00023.bmp` with
`artifacts/leaf-cluster-color/frame-00023.bmp`, and inspected
`artifacts/leaf-cluster-color-moving/frame-00023.bmp` at
`--camera-view 2922 5 3030.75 .65 .3 22`, town with seed 42.
Leaf clusters show less checker-like color contrast. The camera does not show
the entire upper canopy; this is a material comparison, not full tree silhouette
validation. Coarse canopy shapes and sparse, repeated planting remain major gaps.
Teardown parity remains incomplete.

Release shader build passed. All nine renderer checks passed with zero DirectX
errors. Metropolis close/overview/district p95: 12.729 / 2.272 / 12.179 ms,
within the 16.67 ms budget.
GPU conformance passed.

## September 7 staggered broadleaf canopy

A higher full-tree view showed the canopy's horizontal rings clearly. Broadleaf
variants now distribute nine limbs through staggered heights instead of seven
limbs on repeated levels. Smaller, vertically varied fork crowns break up those
rings and expose branching between clusters. Conifers and tree placement are
unchanged; no packages or primary sampling changes.

Compared `artifacts/tree-full-audit/frame-00023.bmp` with
`artifacts/tree-staggered-crown/frame-00023.bmp`, and inspected
`artifacts/tree-staggered-crown-moving/frame-00023.bmp`, at
`--camera-view 2922 7 3030.75 .65 .25 26`, town with seed 42. The full canopy is
visible from this pose and its outline is more irregular. This comparison does
not establish the cause of every horizon-aligned artifact in earlier captures.
Large leaf clusters and sparse repeated planting still look coarse; Teardown
parity remains incomplete.

Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.587 / 2.183 / 12.159 ms, within 16.67 ms.

## September 7 deterministic tree orientation

Voxel tree instances now select one of four quarter-turn orientations from a
stable tile hash. Rotation pivots around the model's (8,8) center, preserving the
existing trunk location and jitter. Current and previous poses match for these
static objects. Quarter turns keep the padded canopy bounds inside the parcel.
Placement, clearing and saved vegetation data are unchanged. No packages added.

Compared `artifacts/tree-staggered-crown/frame-00023.bmp` with
`artifacts/tree-orientation/frame-00023.bmp`, and inspected
`artifacts/tree-orientation-moving/frame-00023.bmp`. Branch profiles vary more
between instances. The four shared models and sparse planting remain apparent;
orientation alone does not achieve natural woodland variety. This rendering
treatment applies to the voxel renderer. Teardown parity remains incomplete.

Release build and all eight tests passed. Added checks for deterministic poses,
fixed model centers, coverage of all four turns, and all model brick corners
remaining within a tile at the extreme placement jitters. Metropolis
close/overview/district p95: 12.493 / 3.082 / 12.192 ms, within 16.67 ms.
All nine renderer checks passed with zero DirectX errors.

## September 7 ground transitions

Grass and dry-soil patches now use continuous, rotated world coordinates rather
than quarter-unit snapped coordinates. A wider soil blend softens the patch
boundaries, while footprint filtering fades fine grass variation at distance.
Fixed pixel-center primary rays remain unchanged. No packages added.

Compared `artifacts/tree-orientation/frame-00023.bmp` with
`artifacts/ground-transition/frame-00023.bmp`, and inspected
`artifacts/ground-transition-moving/frame-00023.bmp`. Square patch boundaries are
less conspicuous; distant ground remains flat and softly patterned. Mean temporal
standard deviation over the last eight stationary frames in the ground crop
(x100..549, y550..849) was 0.080 before and 0.077 after, on a 0..255 RGB scale.
This is a localized check, not evidence that all lighting noise is eliminated.

Release build and GPU conformance passed. Metropolis close/overview/district p95:
12.653 / 2.293 / 12.278 ms, within 16.67 ms. All nine renderer checks passed
with zero DirectX errors.

## September 7 fern understory

Replaced the five coarse root clumps in each voxel tree model with seven small
fern fans. Eighth-unit cells describe rising and falling fronds and paired thin
leaflets; the canopy keeps its existing resolution. The plants merge into the
shared tree model, so orientation, parcel clearance and clearing follow the tree.
No packages added. Primary rays remain fixed at pixel centers.

Compared `artifacts/ground-transition/frame-00023.bmp` with
`artifacts/fern-fronds/frame-00023.bmp`, and inspected
`artifacts/fern-fronds-moving/frame-00023.bmp`. The revised fronds have more depth
and finer gaps than the previous root blobs. The first flat-pad version in
`artifacts/fern-understory` was superseded. The repeated arrangement around trees
and large empty spaces between them remain conspicuous.

Mean temporal standard deviation over the last eight stationary frames in the
root crop (x650..969, y650..809) increased from 0.226 to 0.314 on a 0..255 RGB
scale. Fine geometry adds lighting variation; this pass does not eliminate noise
or moving silhouette aliasing. Teardown parity remains incomplete.

Release build and all eight tests passed, including existing rotated tree bounds
checks. All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 12.432 / 2.308 / 12.395 ms, within 16.67 ms.
Peak close-scene GPU allocation was 669.648 MiB.

## September 7 horizon clouds

The voxel background sky now has a fixed, three-scale procedural high cloud
layer and a faster transition from horizon haze to blue sky. Cloud coverage
fades out at the horizon, where the background still meets the existing aerial
perspective color. Direction-only coordinates have no time or sample dependence.
No packages added and fixed pixel-center primary rays remain unchanged.

Compared `artifacts/fern-fronds/frame-00023.bmp` with
`artifacts/horizon-clouds/frame-00023.bmp`, and inspected
`artifacts/horizon-clouds-moving/frame-00023.bmp`. Soft cloud bands are visible
in the narrow sky area available at the minimum playable pitch. The earlier
`artifacts/high-clouds` version was too faint and was superseded. All RGB pixels
in the top 120 rows were identical across the last eight stationary frames.
This background treatment does not yet supply cloud shadows or cloud detail in
reflections; diffuse sky illumination retains its smooth approximation.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.366 / 2.209 / 12.408 ms, within 16.67 ms. Teardown parity remains incomplete.

## September 7 reflected sky consistency

Glass reflection and transmission rays that miss scene geometry now evaluate
the same hazy, clouded background as primary rays. Rough reflective materials
also sample that environment along their existing roughness-dependent ray
directions. Diffuse illumination retains the smooth sky approximation. No
packages added; fixed pixel-center primary rays remain unchanged.

Compared `artifacts/sky-reflection-before/frame-00023.bmp` with
`artifacts/sky-reflection/frame-00023.bmp`, and inspected
`artifacts/sky-reflection-moving/frame-00023.bmp`. The front-facing house panes
show a subtle difference; this view does not demonstrate strong cloud shapes.
Mean temporal standard deviation in a central pane (x788..821, y365..429) over
the last eight stationary frames was 0.363 both before and after, on a 0..255 RGB
scale. Lighting noise and moving glass noise remain visible. Cloud shadows and
fully integrated sky illumination remain unfinished.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.781 / 2.573 / 12.498 ms, within 16.67 ms. Teardown parity remains incomplete.

## September 7 separated ivy leaves

Residential corner ivy now uses shorter, separated leaf sprays around its woody
runners. Horizontal growth alternates upper and lower leaves, revealing more
brickwork and shutters. The existing quarter-unit model and occupancy remain;
no packages added. Fixed pixel-center primary rays remain unchanged.

Compared `artifacts/sky-reflection/frame-00023.bmp` with
`artifacts/ivy-leaf-gaps/frame-00023.bmp`, and inspected
`artifacts/ivy-leaf-gaps-moving/frame-00023.bmp`. The final version opens visible
gaps across the facade. An initial version (`artifacts/ivy-sprays`) joined into
vertical strips and was superseded. The remaining quarter-unit leaf shapes and
regular spacing still look coarse; Teardown parity remains incomplete.

Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.422 / 2.832 / 12.316 ms, within 16.67 ms.

## September 7 fine ivy geometry

The residential climber now uses a separate eighth-unit canvas for leaves, base
growth and thinner woody runners. Fine ivy is clipped against the coarse facade
before merging; coincident shutter cells are removed against the surviving ivy.
Only the existing ivy-bearing house variant allocates this detail. No packages
added, and primary pixel-center rays remain fixed.

Compared `artifacts/ivy-leaf-gaps/frame-00023.bmp` with
`artifacts/fine-ivy-final/frame-00023.bmp`, and inspected
`artifacts/fine-ivy-final-moving/frame-00023.bmp`. Smaller leaf facets and thinner
stems reduce the ladder appearance; root growth is also less blocky. Regular
spacing and repeated facade layouts remain apparent. Mean temporal standard
deviation in the ivy crop (x490..599, y290..649), last eight stationary frames,
increased from 0.565 to 0.589 on a 0..255 RGB scale. This pass does not eliminate
lighting noise or moving aliasing. Teardown parity remains incomplete.

Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.525 / 3.026 / 12.352 ms, within 16.67 ms. Peak close allocation: 669.902 MiB.

## September 7 stationary leaf accumulation

Validated leaf history now accumulates up to 32 samples when screen motion is
below 0.05 pixels, without clamping that average to the current noisy sample.
Moving foliage retains the existing 16-sample limit and radiance clamp. Object,
material, position and normal rejection remain active. Primary rays remain
fixed at pixel centers; no packages added.

Compared 126-frame stationary captures in `artifacts/leaf-history-before` and
`artifacts/leaf-history`. Mean temporal standard deviation in the ivy crop
(x490..599, y290..649), last sixteen captured frames, fell from 0.791 to 0.692
on a 0..255 RGB scale, about 13%. Inspected `frame-00095.bmp` and the moving
capture `artifacts/leaf-history-moving/frame-00023.bmp`. This localized result
does not establish noise-free foliage. Longer history can delay lighting changes
on stationary leaves; moving silhouette aliasing remains.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.529 / 2.704 / 12.480 ms, within 16.67 ms. Teardown parity remains incomplete.

## September 7 brick scale

Brick courses now use quarter-unit lengths and eighth-unit heights, half the
previous dimensions. Mortar width and footprint filtering were scaled with the
pattern, retaining its distant area average. Removed the independent coarse
voxel color noise from brick and reduced per-brick contrast. No packages added;
primary rays remain fixed at pixel centers.

Compared the previous house capture in `artifacts/leaf-history` with
`artifacts/brick-scale/frame-00023.bmp`. Inspected the close moving capture in
`artifacts/brick-scale-moving` and wider moving view in
`artifacts/brick-scale-wide`. The smaller units reduce the oversized checkerboard
appearance. Brickwork is still a flat material pattern; geometric mortar relief
and broader architectural variety remain unfinished.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.669 / 2.283 / 12.557 ms, within 16.67 ms. Teardown parity remains incomplete.

## September 7 paving scale and filtering

Concrete paving now uses one-unit slabs with thinner joints filtered against
the projected footprint. Joints converge to their area average at distance.
Replaced independent quarter-unit color noise with filtered fine grain and
continuous broad wear; per-slab contrast is more restrained. No packages added.
Primary pixel-center rays remain fixed.

Compared `artifacts/brick-scale/frame-00023.bmp` with
`artifacts/paving-scale/frame-00023.bmp`, and inspected
`artifacts/paving-scale-moving/frame-00023.bmp`. The smaller slabs improve street
scale, but paving remains clean and repetitive. Mean temporal standard deviation
in the pavement crop (x600..1099, y770..819), last eight stationary frames, was
0.163 both before and after on a 0..255 RGB scale. This is a local noise check,
not proof that all distant patterns are alias-free.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.717 / 2.238 / 12.623 ms, within 16.67 ms. Teardown parity remains incomplete.

## September 7 paving wear

Added occasional cooler replacement slabs and thin, bent cracks to the concrete
paving material. Selection and orientation are fixed by slab coordinates; crack
width is filtered and the detail fades with footprint. No packages added and
fixed pixel-center primary rays remain unchanged.

Compared `artifacts/paving-scale/frame-00023.bmp` with
`artifacts/paving-wear/frame-00023.bmp`, and inspected
`artifacts/paving-wear-moving/frame-00023.bmp`. Wear is subtle at the playable
camera distance. The paving remains predominantly clean, and cracks have no
geometric depth. Mean temporal standard deviation in the pavement crop
(x600..1099, y770..819), last eight stationary frames, changed from 0.163 to
0.165 on a 0..255 RGB scale.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.782 / 2.215 / 12.624 ms, within 16.67 ms. Teardown parity remains incomplete.

## September 7 planted flower boxes

Standard residential front planters now contain three layered plants with small
pale blossoms and exposed dark mulch. Eighth-unit foliage replaces the previous
solid quarter-unit clump. The existing planter boxes retain their size; fine
plants merge into the building model after clipping against coarse geometry.
No packages added and fixed primary rays remain unchanged.

Compared `artifacts/paving-wear/frame-00023.bmp` with
`artifacts/planter-flowers/frame-00023.bmp`, and inspected
`artifacts/planter-flowers-moving/frame-00023.bmp`. Individual plants and the
soil surface are more readable. Repeated plant arrangements across houses and
coarse neighboring hedges still limit natural variety. Teardown parity remains
incomplete.

Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.649 / 2.293 / 12.415 ms, within 16.67 ms. Peak close allocation: 670.027 MiB.

## September 7 fine garden hedges

Standard residential side hedges now use narrow eighth-unit detail canvases.
Their upper growth varies in height and alternates its lateral offset, above a
continuous lower hedge body. Fine geometry is clipped against the main building
and merged into the existing model. No packages added; primary rays remain fixed.

Compared `artifacts/planter-flowers/frame-00023.bmp` with
`artifacts/fine-hedges/frame-00023.bmp`, and inspected
`artifacts/fine-hedges-moving/frame-00023.bmp`. Smaller facets and a varied top
edge reduce the row-of-blocks appearance. The hedges still repeat across shared
house variants, and fine foliage retains lighting noise and moving aliasing.
Teardown parity remains incomplete.

Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.771 / 2.315 / 12.577 ms, within 16.67 ms. Peak close allocation: 670.402 MiB.

## September 7 matte painted trim

Pale trim now uses filtered fine grain and restrained broad staining instead of
independent quarter-unit color noise. Roughness increases from 0.6 to 0.75 for
a matte painted finish. This affects all uses of the shared trim material,
including pale decorative details. No packages added; primary rays remain fixed.

Compared `artifacts/fine-hedges/frame-00023.bmp` with
`artifacts/matte-trim/frame-00023.bmp`, and inspected
`artifacts/matte-trim-moving/frame-00023.bmp`. Frames have a more even surface
finish, but geometry remains thick and lighting still mottles some shaded edges.
Mean temporal standard deviation in the upper frame crop (x770..869,
y340..354), last eight stationary frames, changed from 0.388 to 0.402 on a
0..255 RGB scale. This is a material refinement, not a demonstrated noise fix.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.611 / 2.219 / 12.542 ms, within 16.67 ms. Teardown parity remains incomplete.

## September 7 plaster finish

Plaster now uses continuous broad weathering coordinates and filtered fine
aggregate with low-contrast trowel variation. Independent quarter-unit color
noise is excluded from plaster. Other facade materials retain their existing
weathering treatment. No packages added; primary rays remain fixed.

Compared `artifacts/plaster-before/frame-00023.bmp` with
`artifacts/plaster-finish/frame-00023.bmp`, and inspected
`artifacts/plaster-finish-moving/frame-00023.bmp`. The difference on plaster
houses is modest at this distance; the foreground service building uses another
material. These captures do not establish close-range finish quality. A closer
plaster facade comparison remains useful. Teardown parity is incomplete.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.709 / 2.241 / 12.736 ms, within 16.67 ms.

## September 7 fine balcony railings

A close plaster-house audit at camera focus (3896,4,3542), yaw 3.35, pitch 0.25,
distance 22 exposed oversized balcony metalwork. Standard residential variants
1 and 3 now use eighth-unit rails and posts, including lower rails and side
returns, merged into the building model. Balcony slabs retain their existing
geometry. No packages added; primary rays remain fixed.

Compared `artifacts/plaster-close-audit/frame-00023.bmp` with
`artifacts/fine-balcony/frame-00023.bmp`, and inspected
`artifacts/fine-balcony-moving/frame-00023.bmp`. Slimmer rails reveal more window
area and read more clearly as metal. The audit also shows that plaster lighting
remains mottled in shade and window sash geometry is still heavy. Thin metal
can still alias in motion. Teardown parity remains incomplete.

Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
12.652 / 2.263 / 12.534 ms, within 16.67 ms. Peak close allocation: 670.715 MiB.

## September 7 fine window dividers

Standard residential timber window dividers now use eighth-unit geometry on
all four facades. Bars sit immediately outside the glass, preserving clear pane
coverage behind them. The entrance opening skips the otherwise generated middle
ground-floor sash. Fine detail merges with the existing building and respects
facade and ivy clipping. No packages added; primary rays remain fixed.

Compared `artifacts/fine-balcony/frame-00023.bmp` with
`artifacts/fine-sash-final/frame-00023.bmp`, and inspected
`artifacts/fine-sash-final-moving/frame-00023.bmp`. More glass is visible through
the slimmer timber crosses. An initial capture in `artifacts/fine-window-sash`
exposed a divider over the entrance; the final version removes it. Window trim
remains thick and moving glass lighting is noisy. Teardown parity is incomplete.

Release build and all eight tests passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
13.263 / 2.394 / 13.125 ms, within 16.67 ms. Peak close allocation: 671.855 MiB.
The added detail and increased visible glass raise the close-view rendering cost
relative to the previous pass (12.652 ms p95).

## September 7 stationary matte accumulation

Extended the validated stationary-leaf accumulation treatment to all matte
surfaces (roughness at least 0.65). At less than 0.05 pixels of motion, these
surfaces retain up to 32 samples without clamping history to the current noisy
sample. Moving matte surfaces retain their previous history limit and clamp.
Object, position, material and normal validation remain active. Primary rays
remain fixed; no packages added.

Compared 126-frame captures in `artifacts/matte-history-before` and
`artifacts/matte-history`. Mean temporal standard deviation in the shaded wall
crop (x580..1049, y285..329), last sixteen captured frames, fell from 2.066 to
1.034 on a 0..255 RGB scale, about 50%. Inspected `frame-00095.bmp` and
`artifacts/matte-history-moving/frame-00023.bmp`. Residual mottling and moving
noise remain. Longer history can delay changing illumination on stationary
surfaces; the measurement is not proof of noise-free rendering.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
13.482 / 2.334 / 13.084 ms, within 16.67 ms. Teardown parity remains incomplete.

## September 7 wider wall lighting filter

Plaster, industrial concrete and service facades now use a 5x5 half-resolution
lighting neighborhood with a wider Gaussian. Other materials keep the existing
3x3 footprint. Object, material, normal and plane weighting remain active, and
full-resolution albedo compensation preserves the surface finish. Primary rays
remain fixed; no packages added.

Compared 126-frame captures in `artifacts/matte-history` and
`artifacts/wall-spatial`. Mean temporal standard deviation in the shaded wall
crop (x580..1049, y285..329), last sixteen frames, fell from 1.034 to 0.724 on
a 0..255 RGB scale, about 30%. Inspected
`artifacts/wall-spatial-moving/frame-00023.bmp`: wall lighting appears smoother
and window/railing boundaries remain distinct in this view. The wider filter
can soften small lighting features, and the result does not establish general
absence of edge leaks or moving noise. Teardown parity remains incomplete.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
13.387 / 2.314 / 13.338 ms, within 16.67 ms.

## September 7 foliage palette reference review

Revisited the saved Teardown reference (`artifacts/teardown-reference.jpg`)
alongside the city and forest captures. Its warmer foliage, layered planting,
varied silhouettes and dense scene dressing remain materially different from
our flat, sparse landscape and repeated architecture. A palette adjustment alone
cannot establish parity; canopy structure and scene density deserve priority.

Adjusted the shared leaf palette toward warmer broadleaf greens and deeper,
less cyan conifer greens. Compared `artifacts/foliage-palette-before/frame-00023.bmp`
with `artifacts/foliage-palette/frame-00023.bmp`, and inspected
`artifacts/foliage-palette-moving/frame-00023.bmp`. The color distinction is
clearer, while coarse canopy lobes and sparse planting remain conspicuous.
No packages added; lighting response and fixed primary rays remain unchanged.

Release build and GPU conformance passed. All nine renderer checks passed with
zero DirectX errors. Metropolis close/overview/district p95:
13.537 / 2.486 / 13.275 ms, within 16.67 ms. Teardown parity remains incomplete.

## September 7 broadleaf branch-end clusters

Split each broadleaf fork into three smaller offset foliage sprays and staggered
the upper crowns. A compact central crown covers the trunk leader; the first
version exposed bark to the vertical canopy ray and failed VegetationTests.
The corrected geometry passes all eight Release tests, including GPU conformance.

Compared `artifacts/foliage-palette/frame-00023.bmp` with
`artifacts/canopy-sprays-final/frame-00023.bmp`, and inspected
`artifacts/canopy-sprays-moving/frame-00023.bmp`. The foreground tree has smaller
lobes and more visible branch gaps, although the quarter-unit leaves still look
coarse and the flat landscape remains sparse. Fixed primary pixel-center rays
are preserved. This geometry pass does not establish a reduction in temporal
lighting noise or Teardown parity.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.294 / 2.294 / 13.152 ms, within 16.67 ms.

## September 7 finer shared tree geometry

Tree models now use eighth-unit cells, giving the broadleaf edges, conifer fans
and tapered limbs finer geometry. Compared `artifacts/fine-tree/frame-00023.bmp`
against `artifacts/canopy-sprays-final/frame-00023.bmp`: leaf clusters have less
stair-stepped edges and conifer fans read more clearly as thin branches. Large
rounded broadleaf masses and sparse landscape composition still need work.

The initial GPU traversal check had zero mismatches but only 80 hits, below its
existing minimum of 100. Its random origins used a fixed three-unit radius even
around smaller bricks. Origins now scale with each brick's dimensions, retaining
the same hit minimum, CPU comparison, axis-aligned rays and ignored-material
cases. This exercises the finer geometry instead of predominantly missing it.

All eight Release tests and nine renderer checks passed with zero DirectX errors.
Metropolis close/overview/district p95: 13.323 / 2.345 / 13.455 ms. Close-view
peak video memory was 673.797 MiB, versus 671.855 MiB in the previous pass.
Inspected `artifacts/fine-tree-moving/frame-00023.bmp` as well. No packages were
added and fixed primary pixel-center sampling remains intact. Finer silhouettes
do not resolve distant edge aliasing or establish Teardown parity.

## September 7 broadleaf shrub layer

Added three offset shrub clumps to each broadleaf model, with five woody shoots
and paired leaf sprays per clump. Heights vary between 1.25 and 2 units before
leaf tips. These share tree placement, rotation and clearing, and stay inside
the existing parcel bounds. No packages or simulation behavior were added.

Compared `artifacts/fine-tree/frame-00023.bmp` with
`artifacts/forest-shrubs/frame-00023.bmp`, and inspected
`artifacts/forest-shrubs-moving/frame-00023.bmp`. The shrubs add a middle layer
above the fern beds, but isolated clumps and their repeated arrangement still
look planted rather than like the dense, overlapping vegetation in the reference.
More varied grouping and ground coverage remain necessary for parity.

The trunk test initially hit new foreground foliage. Its origin now sits inside
the shrub ring and still requires a bark intersection. The corrected vegetation
test passed; the other seven tests, including GPU traversal, passed the build.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.252 / 2.404 / 13.785 ms, within 16.67 ms.
Fixed primary pixel-center rays remain unchanged. Teardown parity is incomplete.

## September 7 overlapping shrub patches

Replaced the evenly spaced three-shrub ring with five clumps in an asymmetric
patch. Clump heights span 0.875..2.125 units before leaf tips; lower foliage
connects the woody shoots visually to the fern layer. Existing tree poses rotate
the whole patch, and the opposite side remains open around the trunk.

Compared `artifacts/forest-shrubs/frame-00023.bmp` with
`artifacts/shrub-patches/frame-00023.bmp`, and inspected
`artifacts/shrub-patches-moving/frame-00023.bmp`. The patch looks more connected
and less like individual planted shrubs. Repeated tree forms and large empty
spaces across the landscape remain conspicuous. All eight Release tests passed;
no packages were added and fixed primary sampling remains intact.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.313 / 2.391 / 13.748 ms, within 16.67 ms.
The metropolis capture still shows repeated facades and mostly empty lot edges;
forest detail alone does not address those larger composition gaps.

## September 7 residential dormers

Added paired street-facing dormers to residential variant 3. Masonry cheeks,
recessed glazing, timber dividers, projecting sills and thin metal caps interrupt
the previously continuous roof plane. Window apertures cut through the cheeks
into a hollow recess. Metal caps retain their actual normals rather than the
main tiled roof's slope proxy.

Inspected `artifacts/dormer-street/frame-00023.bmp`,
`artifacts/dormer-roof/frame-00023.bmp` and
`artifacts/dormer-moving/frame-00023.bmp`. The roof now has a distinct silhouette
and visible window depth from both viewing heights. The repeated lower facade
grid, heavy trim and stepped roof shadows remain visible limitations.

All eight Release tests passed on the initial geometry; voxel and GPU tests
passed again after opening the glazing recess through the wall. No packages
added; stationary primary sampling remains intact. Parity is not established.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.529 / 2.306 / 13.698 ms, within 16.67 ms.

## September 7 residential window surrounds

Moved standard residential facade surrounds and sills into the existing
eighth-unit detail canvases. Surround width is now 0.125 units; sills are thinner
and narrower, and timber sash bars sit immediately ahead of the glazing.
Commercial frames, attic windows, dormers and entrance surrounds retain their
existing construction. Fine details continue to clip against occupied wall cells.

Compared `artifacts/dormer-street/frame-00023.bmp` with
`artifacts/fine-surrounds/frame-00023.bmp`, and inspected
`artifacts/fine-surrounds-brick-moving/frame-00023.bmp`. The lower facade frames
look less bulky while the sill projection and glass depth remain visible. Attic
trim is now noticeably heavier than the main windows and remains a future detail
pass. All eight Release tests passed; no packages were added.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 14.528 / 2.401 / 14.766 ms, within 16.67 ms.
Close-view GPU time rose from 12.389 to 13.536 ms and peak video memory from
674.320 to 676.402 MiB in these runs. The finer facade geometry has a measurable
cost; further detail must respect the reduced frame-time headroom. Stationary
primary sampling remains intact and Teardown parity remains incomplete.

## September 7 tighter facade detail bounds

Fine building detail bricks now crop empty boundary planes to power-of-two
extents. Repeating source cells into the fixed 8-cubed payload retains the
occupied world volumes while reducing empty AABB intersections. This applies to
the existing facade, balcony, garden, ivy and hedge detail canvases. It does not
reduce the authored detail or add packages.

Added exact front/back/side trim-depth checks for the three standard residential
variants, and included their facade models in the GPU/CPU traversal comparison.
All eight tests passed initially; expanded voxel and GPU tests passed afterward.

Inspected `artifacts/tight-detail/frame-00023.bmp` and
`artifacts/tight-detail-moving/frame-00023.bmp` against the preceding fine-surround
captures. Mean absolute RGB difference was 0.00191 stationary and 0.00115 moving
on a 0..255 scale. No visible geometry change was found in these views. This
supports retaining the finer frames within the rendering budget; it does not
establish broad visual parity or remove the remaining lighting limitations.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.029 / 2.458 / 13.628 ms. Close GPU time fell from
13.536 to 12.043 ms; peak video memory remained 676.402 MiB in these runs.

## September 7 sun disk softness

Sun directions now sample a tangent-plane disk with radius 0.035, replacing the
previous hemisphere perturbation scaled by 0.018. This artistic source size
slightly broadens cast-shadow penumbras, without changing primary ray positions
or adding shadow rays. Shader compilation and expanded GPU conformance passed.

Compared the settled frames in `artifacts/sun-disk-before` and
`artifacts/sun-disk` (126-frame runs), and inspected
`artifacts/sun-disk-moving/frame-00023.bmp`. Dormer shadow edges are modestly
softer; visible roof steps remain. Last-sixteen-frame temporal standard deviation
on a 0..255 RGB scale increased from 0.282 to 0.307 in the dormer shadow crop
(x740..834, y280..409), while the plaster crop (x580..1039, y470..499) changed
from 0.639 to 0.640. This is a softness/noise tradeoff, not a shimmer fix or proof
of parity. No packages were added.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.270 / 2.437 / 13.680 ms, within 16.67 ms.

## September 7 direct-sun sample sequence

Primary-surface sunlight now uses a pixel-scrambled low-discrepancy disk sequence
instead of independently hashed directions each frame. The sequence wraps after
1024 frames; secondary lighting retains its existing random stream. Source size,
ray count and fixed primary camera rays are unchanged. Shader build and expanded
GPU traversal conformance passed.

Compared 126-frame runs `artifacts/sun-disk` and `artifacts/sun-sequence`.
Last-sixteen-frame temporal standard deviation in the same dormer-shadow crop
(x740..834, y280..409) fell from 0.307 to 0.288 on a 0..255 RGB scale, about 6%.
The shaded plaster crop (x580..1039, y470..499) stayed effectively unchanged at
0.640, so this does not address its secondary-lighting noise. Inspected the final
stationary frame and `artifacts/sun-sequence-moving/frame-00023.bmp`; softer
shadow edges remain visible. No packages added; parity remains incomplete.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.337 / 2.366 / 14.081 ms, within 16.67 ms.

## September 7 diffuse-bounce sample sequence

Primary surfaces now distribute diffuse-bounce directions with an independently
scrambled low-discrepancy sequence. High quality's two rays occupy consecutive
sequence positions. The cosine-weighted hemisphere distribution, ray count,
bounce distance and fixed primary camera rays are retained. Glass interior
sampling continues to use its existing path. No packages were added.

Compared `artifacts/sun-sequence` and `artifacts/bounce-sequence`, both 126-frame
runs. Last-sixteen-frame temporal standard deviation fell from 0.288 to 0.236
in the dormer-shadow crop (x740..834, y280..409), about 18%, and from 0.640 to
0.615 in the shaded plaster crop (x580..1039, y470..499), about 4%, on a 0..255
RGB scale. Inspected the settled final frame and
`artifacts/bounce-sequence-moving/frame-00023.bmp`. Noise remains and the narrow
crop comparison does not establish general stability or Teardown parity.

Shader build and expanded GPU conformance passed.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.346 / 2.429 / 13.776 ms, within 16.67 ms.

## September 7 attic window proportions

Reduced standard residential gable-window trim from quarter-unit to eighth-unit
width and halved the timber divider width, placing it back at the glazing plane.
This brings the attic detail closer to the refined lower windows. The aperture
size and roof silhouette remain unchanged; dormer construction is separate.

Inspected `artifacts/attic-trim/frame-00023.bmp` and
`artifacts/attic-trim-moving/frame-00023.bmp`, comparing the latter with
`artifacts/tight-detail-moving/frame-00023.bmp`. The attic frame looks less
oversized and both panes remain readable. This art pass does not establish a
lighting improvement; existing roof steps and broad facade repetition remain.
All eight Release tests passed. No packages added or camera sampling changes.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.373 / 2.420 / 14.055 ms, within 16.67 ms.

## September 7 park planting and seating

Rebuilt the park asset on an eighth-unit canvas. Four trees now have bark trunks,
visible branches, smaller offset crowns and varied heights. Low planting beds
anchor the trunks. Two slatted timber benches have open backs and metal legs;
short paved branches connect the central walk to the seats. Restored the original
entrance paving after the canvas replacement. Tight bounds limit empty traversal.

Compared `artifacts/park-before/frame-00023.bmp` with
`artifacts/park-final/frame-00023.bmp`, and inspected
`artifacts/park-final-moving/frame-00023.bmp`. Branch openings, seat slats and
ground planting are visible improvements over the simple original trees and
solid bench. The park still has a formal, repeated layout and the wider city
remains sparse; Teardown parity is incomplete.

All eight Release tests passed on the initial rebuild. Voxel/GPU checks passed
after the seat refinement, and voxel checks passed after the final paving change.
No packages or simulation changes; fixed primary sampling remains intact.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.566 / 2.808 / 13.978 ms, within 16.67 ms.

## September 7 cafe seating detail

Replaced the commercial frontage's block tables and stools with round timber
tables, metal pedestal bases, two open-backed chairs per table and small planted
pots. Eighth-unit geometry uses the existing clipped detail merge and tight
bounds. The paired seating groups leave the glazed entrance clear and stay on
the shop frontage; no packages or simulation behavior were added.

Compared `artifacts/cafe-seating-before/frame-00023.bmp` with
`artifacts/cafe-seating/frame-00023.bmp`, and inspected
`artifacts/cafe-seating-moving/frame-00023.bmp`. Furniture now reads as tables
and chairs with open space beneath and behind them. The large awning steps,
oversized upper-window trim and noisy moving glass remain conspicuous.
All eight Release tests passed. Camera sampling is unchanged; parity remains
incomplete.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.530 / 2.407 / 14.086 ms, within 16.67 ms.

## September 7 fine cafe awning

Moved the shop awning into the eighth-unit frontage detail canvas. The canopy
is thinner with finer slope steps, a scalloped valance and narrow metal support
braces. Existing green/cream stripe materials and the sign remain unchanged.
Tight detail bounds and wall clipping also apply to the canopy.

Compared `artifacts/cafe-seating/frame-00023.bmp` with
`artifacts/fine-awning/frame-00023.bmp`, and inspected
`artifacts/fine-awning-moving/frame-00023.bmp`. The awning reads less like a
thick slab, and the hanging edge remains readable. Its stepped slope, heavy
commercial window trim and moving glass noise still limit fidelity.
All eight Release tests passed. No packages or primary camera sampling changes.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.380 / 2.402 / 13.941 ms, within 16.67 ms.
Teardown parity remains incomplete.

## September 7 commercial window frames

Extended the fine facade-frame construction to commercial buildings, including
three-unit-wide windows. Shops use thin metal dividers and projecting sills;
odd variants also have a horizontal crossbar. Updated adjacent-support clipping
to retain metal beside glass rather than treating glazing as opaque masonry.

Compared `artifacts/fine-awning/frame-00023.bmp` with
`artifacts/shop-frames/frame-00023.bmp`, and inspected
`artifacts/shop-frames-moving/frame-00023.bmp`. The surrounds look less bulky
and both panes remain visible. Confirmed the wide-window variant in
`artifacts/shop-wide-final/frame-00023.bmp` (camera target 4040,4,3590).
The wide shop's coarse wall variation and moving glass noise remain limitations.

All eight tests passed initially; voxel and GPU checks passed after the clipping
adjustment. All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.375 / 2.388 / 14.024 ms, within 16.67 ms.
No packages or primary sampling changes. Teardown parity remains incomplete.

## September 7 commercial wall finish

Removed coarse per-voxel color variation from the shared commercial wall
material. It now uses continuous weathering coordinates and the filtered fine
grain/trowel finish already used by plaster, retaining its existing base color.

Compared `artifacts/shop-wide-final/frame-00023.bmp` with
`artifacts/shop-finish/frame-00023.bmp`, and inspected
`artifacts/shop-finish-moving/frame-00023.bmp`. The checkerboard blocks on the
wide-window shop are gone, while broad weathering remains. Lighting noise is
still visible, particularly in moving glass; this material pass does not prove
improved temporal stability. Shader build and GPU conformance passed.
No packages or primary sampling changes. Teardown parity remains incomplete.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.625 / 2.428 / 14.278 ms, within 16.67 ms.

## September 7 window lighting samples

Stratified the existing room-bounce rays with a randomly rotated Hammersley
point set. Ray counts remain four/two/one for high/medium/low quality. Primary
rays remain fixed at pixel centers, preserving the stationary-edge fix.

Compared the last 16 frames of `artifacts/glass-samples-before` and
`artifacts/glass-samples-after` at camera 4040,4,3590, yaw 3.35, pitch .25,
range 22. Mean RGB temporal standard deviation (0..255) decreased from
0.61565 to 0.60262 in the left pane (x595..659, y305..329), and from
0.58089 to 0.57119 in the right pane (x855..924, y300..319). The wall
control stayed at 0.60551. This is a small noise reduction, not elimination.
Inspected `artifacts/glass-samples-moving/frame-00023.bmp`; moving glass
still shows noise. GPU conformance passed: 8,192 rays, zero DirectX errors.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.565 / 2.431 / 14.058 ms, within 16.67 ms.

## September 7 continuous room-light sequence

Window room bounces now continue a pixel-scrambled R2 sequence across frames,
using the existing four/two/one rays per quality level. Both regular half-resolution
shading and the thin-glass fallback provide pixel coordinates. Primary rays and
history limits remain unchanged; no packages added.

The first experiment rotated the per-frame Hammersley set over time. It increased
noise and was rejected (`artifacts/glass-history`). The retained continuous sequence
is captured in `artifacts/glass-continuous` and `artifacts/glass-continuous-moving`,
at the same camera as the previous pass. Compared with `glass-samples-after`, mean
RGB temporal standard deviation over the last 32 stationary frames changed:

- Left window, x585..761/y300..376: 0.61746 to 0.56227 (9% lower).
- Right window, x856..1008/y296..364: 0.68844 to 0.65086 (5% lower).
- Lower shop glazing, x600..744/y580..644: 0.42643 to 0.33502 (21% lower).

These larger rectangles include frame/interior structure. Improvement is not
uniform: the previous small right-pane crop over 16 frames rose from 0.57119 to
0.61336, while the small left crop fell from 0.60262 to 0.56594. The wall control
was unchanged at 0.60551. Inspected the moving capture: glass remains visibly
grainy, and this pass does not establish improved moving-image noise numerically.
GPU conformance passed with 8,192 rays and zero DirectX errors.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.550 / 2.465 / 14.318 ms, within 16.67 ms.
Teardown parity remains incomplete.

## September 7 older shop facade

Commercial variant 1 now has narrow masonry piers with projecting bases and
capitals, a thin horizontal band, and a stepped dentilled cornice. The detail
uses the existing one-eighth-unit front facade canvas and tight primitive bounds.
Other shop variants retain their plainer elevations. Restricting the design to
variant 1 leaves sufficient clearance above the highest window row at each level.
No shader, camera sampling, occupancy or package changes.

Compared `artifacts/glass-continuous/frame-00023.bmp` with
`artifacts/shop-cornice/frame-00023.bmp`, and inspected
`artifacts/shop-cornice-moving/frame-00023.bmp`. The formerly blank upper wall
now has shadowed relief and a distinct older masonry character. The panes stay
clear and the trim remains readable while moving. The heavy roof fascia and
repeated overall building massing still limit the result.

All eight Release tests passed initially; voxel and GPU conformance passed after
restricting the trim to the taller variant. Teardown parity remains incomplete.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.414 / 2.374 / 14.109 ms, within 16.67 ms.
Peak metropolis video memory was 682.469 MiB (previous pass: 680.656 MiB).

## September 7 commercial roof construction

Commercial roofs now use a one-eighth-unit detail canvas with a narrow concrete
slab lip, a recessed dark membrane, masonry parapets and thin projecting metal
coping. The ceiling still closes the rooms, and the existing vent housings remain
on the deck. The new roof replaces the oversized brown tiled slab on every
commercial variant. Other building roofs are unchanged.

Compared `artifacts/shop-roof-before/frame-00023.bmp` with
`artifacts/shop-roof-detail/frame-00023.bmp` at camera 4040,7,3592, yaw 2.7,
pitch .55, range 23. Also inspected `artifacts/shop-roof-street` against
`artifacts/shop-cornice` and the elevated `artifacts/shop-roof-moving` capture.
The roof edge is lighter and the deck, parapet and equipment read as separate
construction elements. The membrane's coarse material variation and blocky vent
housings remain visible limitations. All eight Release tests passed. No shader,
primary sampling or package changes. Teardown parity remains incomplete.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.371 / 2.420 / 14.207 ms, within 16.67 ms.
Peak metropolis video memory was 685.312 MiB (previous pass: 682.469 MiB).

## September 7 rooftop equipment

Replaced the two coarse commercial rooftop boxes with a compact twin-fan
condenser and a capped exhaust. The condenser has a raised curb, front louvers,
dark circular fan recesses, blades and hubs. The exhaust has a dark opening and
corner supports beneath its overhanging cap. Thin conduit follows a right-angle
route across the deck. All use the existing fine roof canvas and materials;
other building equipment is unchanged. Fans are static art.

Compared `artifacts/shop-roof-detail/frame-00023.bmp` with
`artifacts/shop-roof-equipment/frame-00023.bmp`, and inspected
`artifacts/shop-roof-equipment-moving/frame-00023.bmp`. The smaller housings,
fan openings and exhaust cap remain legible in motion and give the roof a more
credible scale. The deck's coarse shading and repeated equipment arrangement
remain limitations. All eight Release tests passed. No shader, camera sampling
or package changes. Teardown parity remains incomplete.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.451 / 2.388 / 14.112 ms, within 16.67 ms.
Peak metropolis video memory was 685.500 MiB (previous pass: 685.312 MiB).

## September 7 roof membrane material

Added palette material 25 (`VMembrane`) for commercial roof decks. It retains
the previous dark base color but uses continuous low-contrast weathering,
footprint-filtered aggregate grain and narrow lap seams at 1.5-unit spacing.
Seams fade toward their area average at distance. Roughness is .95, with no
metallic response. Existing rubber objects keep their original material.

Compared `artifacts/shop-roof-equipment/frame-00023.bmp` with
`artifacts/shop-membrane/frame-00023.bmp`, and inspected
`artifacts/shop-membrane-moving/frame-00023.bmp`. The quarter-unit checkerboard
is gone and the deck reads as continuous rolled roofing. Thin seams remain
subordinate to equipment and parapets. This is a material appearance comparison,
not a measured temporal-noise improvement. All eight Release tests passed.
No packages or primary camera sampling changes. Teardown parity remains incomplete.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.726 / 2.435 / 14.506 ms, within 16.67 ms.

## September 7 shop service props

Added two wheeled bins, a slatted pallet and open wooden delivery crates to the
existing paved strip beside commercial buildings. Odd variants stack two crates;
even variants use one. Props use a small one-eighth-unit canvas with existing
facade clipping and tight bounds. They remain within the original parcel and
leave the windows clear. No new paving, logical occupancy or package changes.

Inspected `artifacts/shop-yard/frame-00023.bmp` at camera 4043,4,3598,
yaw 2.25, pitch .8, range 24, and `artifacts/shop-yard-moving/frame-00023.bmp`
at 4046,1,3599, yaw 2.25, pitch 1.1, range 18. The props occupy the side strip
and the crates have readable open slats. Nearby roofs obscure this area from
street-level viewpoints. The attempted `shop-yard-before` capture landed inside
a neighboring roof and is not valid comparison evidence. This pass adds local
service detail but does not solve the large empty lots. All eight Release tests
passed. No shader or primary sampling changes. Teardown parity remains incomplete.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 13.444 / 2.343 / 14.069 ms, within 16.67 ms.

## September 7 open-ground scrub

Added a fifth vegetation variant: a low patch of seven uneven woody shrubs with
separate leaf sprays and basal foliage. Existing tree eligibility and variants
are unchanged. Previously unselected tiles with the deterministic sample below
45/1000 can receive scrub; road, parcel and persistent clearing masks still apply.
The default empty-world test now contains 26,191 vegetation instances: the original
17,693 tree placements plus 8,498 scrub patches. This changes visual vegetation
generation, including uncleared land in existing saves, without changing the save
format or construction rules.

Both voxel and legacy renderers support the fifth variant. GPU conformance now
includes the shrub model. Tests retain bounds, determinism, clearing and save/load
checks, and distinguish treeless clearings from chunks without any low vegetation.
The initial full run passed seven targets; the obsolete empty-chunk assumption
was corrected and vegetation tests passed. Vegetation and GPU tests also passed
after the final crown adjustment.

Inspected `artifacts/open-scrub-balanced/frame-00023.bmp` and the matching
`artifacts/open-scrub-balanced-moving` capture at camera 4248,1,3656, yaw 2.7,
pitch .7, range 24. The first `open-lot-shrubs` model looked too tightly clipped;
the final model exposes more branching and varies crown heights. It adds low
cover to an open town lot, but repeated patch shapes and extensive bare ground
remain. No packages or primary camera sampling changes. Teardown parity remains
incomplete.

All nine renderer checks passed with zero DirectX errors. Metropolis
close/overview/district p95: 14.998 / 4.249 / 16.311 ms, within the 16.67 ms
budget but with little district headroom. Peak metropolis video memory was
705.664 MiB. Non-benchmark low/medium/legacy p95 was about 17 ms; those checks
validate execution and DirectX errors, not the benchmark frame-time budget.

## September 7 scrub palette

The voxel renderer now selects one of the two darker existing foliage colors
for each scrub patch, deterministically from its placement tile. Tree and garden
foliage retain their original colors. The final change is in instance setup;
leaf shaders, geometry, placement and primary sampling are unchanged. No packages.

Compared `artifacts/open-scrub-balanced/frame-00023.bmp` with
`artifacts/scrub-instance-palette/frame-00023.bmp`, and inspected the matching
`artifacts/scrub-instance-palette-moving` capture. The scrub is less fluorescent
and retains readable clusters. Repeated shapes remain conspicuous. Build and
GPU conformance passed.

A custom olive/dry-spray shader experiment (`artifacts/scrub-palette`) was not
retained. It added per-ray palette work while performance validation was already
failing: edit-stress p95 17.225 / 16.731 ms, close city 18.075 ms, district
18.985 ms. A controlled edit-stress run with the previous shader also missed at
16.930 ms (GPU 2.727 ms), so that failure was not established as palette-caused.
The final implementation restores the original leaf shader and computes the
color choice per instance. All nine original check commands are collected with
budget failures reported rather than aborting; the repository verification script
and its thresholds are unchanged. Teardown parity remains incomplete.

Final measurements: all nine commands completed with zero DirectX errors.
Metropolis close/overview/district p95: 15.378 / 4.317 / 16.475 ms, meeting
the 16.67 ms budget with little district headroom. Edit-stress still failed at
17.397 ms (GPU 2.739 ms), so the full performance gate remains unresolved.
Low/medium/legacy non-benchmark p95 was 17.5â€“17.8 ms; those commands have no
frame-time assertion in the original verifier.

## September 7 shared terrain models

The renderer now preserves a dirty chunk's GPU model when its generated primitive
records and packed words are identical. It also shares matching terrain models
across chunks. A sampled hash only selects candidates; full equality checks
prevent collisions from changing geometry. Weak cache references and periodic
cleanup avoid retaining retired models. Scene packing includes each shared model
once, while chunk instance identities and transforms stay distinct.

GPU conformance passed. The edit-stress rendered world matches the previous build
pixel-for-pixel: all changed pixels are inside the live diagnostic panel
(x2241..2402, y115..615). Central image x330..2199 is identical at full height.
Compared `artifacts/edit-model-before.json` with the final edit report: p95
17.397 to 14.637 ms, packing 1.981 to 0.079 ms, peak video memory 670.207 to
588.570 MiB. Terrain CPU time rose from 3.876 to 4.465 ms, but reduced packing
and memory traffic improved total frame time. No art, shader, package or primary
sampling changes.

The regular verifier passed edit-stress but stopped at close-city p95 17.357 ms.
The remaining commands are collected separately with the original thresholds
reported, not weakened in the repository verifier. Teardown parity and the full
performance gate remain incomplete.

All nine commands completed with zero DirectX errors. Final metropolis
close/overview/district p95: 17.357 / 5.036 / 19.197 ms. Close and district
remain above 16.67 ms; this pass fixes the measured edit-stress packing cost,
not the remaining GPU-heavy city workloads.

## September 7 room shadow rejection

Window lighting now skips sun-visibility queries when the room surface or its
diffuse-bounce surface faces away from the sampled sun direction. Those direct
contributions are multiplied by a zero cosine, so their shadow result cannot
contribute. Sun directions, random-stream advancement, room bounce counts,
reflection rays and primary sampling are unchanged. No packages added.

Compared all 24 frames of `artifacts/window-shadow-before` and
`artifacts/window-shadow-after` at camera 4040,4,3590, yaw 3.35, pitch .25,
range 22. Mean absolute RGB difference was 0.0001033 on a 0..255 scale; the
maximum difference was 6, with 9,914 changed channels out of 103,680,000.
Inspected the final frame: no visible lighting change. The result is not bitwise
identical. Shader build and GPU conformance passed.

The regular verifier passed edit-stress at 14.569 ms but stopped at close-city
p95 17.715 ms. Close average GPU time was 14.818 ms versus 14.911 ms previously;
that small change does not establish a substantial performance improvement.
The remaining original commands are collected separately without changing the
repository verifier or budgets. Teardown parity remains incomplete.

All nine commands completed with zero DirectX errors. Metropolis
close/overview/district p95: 17.715 / 5.215 / 18.227 ms. District average GPU
time was 15.401 ms, down from 16.049 ms in the preceding pass, but close and
district still exceed the 16.67 ms frame budget.

## September 7 lighting-filter work reduction

Spatial lighting reconstruction now rejects neighbors with mismatched instance,
material or incompatible normals before loading their radiance/albedo or computing
distance weights. The accepted neighborhood and weights remain the same. Normal
agreement to the sixteenth power uses four squarings instead of `pow`.

DXC disassembly confirms 25 logarithm/exponential pairs were removed from the
resolve shader (`artifacts/resolve-before.txt` versus `resolve-after.txt`: Log
31 to 6, Exp 68 to 43). This proves reduced instruction work, not a specific
frame-time gain. Compared 24 frames of `window-shadow-after` with `spatial-weight`:
mean absolute RGB difference 0.0001224 on a 0..255 scale, maximum 6, with 11,321
changed channels out of 103,680,000. Inspected the final frame; no visible change.
The intermediate `spatial-reject` capture covers early rejection alone.

Build and GPU conformance passed. The standard verifier passed edit-stress at
14.374 ms but stopped at close-city p95 17.138 ms. Close resolve time was
2.199 ms, so this run does not establish a material speedup for that view.
Remaining original commands are collected separately; verification thresholds
are unchanged. No packages, art or primary sampling changes. Teardown parity
remains incomplete.

All nine commands completed with zero DirectX errors. Metropolis
close/overview/district p95: 17.138 / 5.013 / 17.919 ms. Close and district
remain above 16.67 ms; the full performance gate is not yet satisfied.

## September 7 rejected lighting-power experiment

Tested multiplication forms for fifth-, sixth- and twelfth-power terms in Fresnel
and sky lighting. DXC lighting disassembly changed from 36 Log / 60 Exp operations
to 0 Log / 12 Exp operations. Across 24 cafe frames (`spatial-weight` versus
`lighting-powers`), mean absolute RGB difference was 0.0000882 on a 0..255 scale,
maximum 6, with no visible change. GPU conformance passed.

The instruction reduction did not improve the measured workloads. Close-city
p95 / average GPU changed from 17.138 / 14.640 ms to 17.656 / 14.908 ms;
district changed from 17.919 / 15.005 ms to 17.985 / 15.309 ms. All nine commands
completed with zero DirectX errors, but close and district missed budget.
Rejected the experiment and restored the prior shader. Rebuild and GPU conformance
passed, and restored lighting disassembly matches the pre-experiment baseline.
The standard report files currently describe the rejected experiment, not a new
benchmark run of the restored build. No packages, retained art or sampling changes.

This evidence argues against further speculative integer-power substitutions
as the next performance step. Investigate voxel traversal and representation cost
instead. Teardown parity and the city performance gate remain incomplete.

## September 7 stationary-camera shimmer

The renderer now detects an exactly unchanged camera and projection, excluding history resets. Static surfaces reuse their exact previous pixel and accumulate up to 256 lighting samples without stochastic radiance clipping. Moving cameras retain existing reprojection and history limits; dynamic instances retain short history. Geometry/style/quality changes still invalidate history.

Release VoxelCity and VoxelGpuTests built successfully; GPU conformance passed. Matching 126-frame cafe captures (`artifacts/still-before` and `artifacts/still-after`), measured over their final 16 frames, reduced mean adjacent-frame RGB difference from 0.17648 to 0.04310 (75.6%) and temporal standard deviation from 0.31170 to 0.10358 (66.8%). Values use 0–255 RGB units. The final stationary image was visually inspected. A 54-frame moving-camera capture with the debug layer also exited successfully. This reduces shimmer rather than freezing illumination; residual sampling noise and slower response to indirect lighting changes remain possible at rest.

## September 7 solid cropped-detail representation

Fine voxel canvases now reclassify a cropped brick as homogeneous only when all 512 remaining cells contain the same nonzero material. Such bricks preserve their cropped bounds and use the existing solid-box intersection path, omitting their 128-word payload. Mixed-material bricks and bricks containing holes keep their original packed representation.

All eight Release tests passed, including CPU intersection checks, fine building-detail coverage, vegetation, and GPU conformance. The metropolis close benchmark reduced packed voxel storage from 9.303 to 8.701 MiB (6.5%). GPU time was mixed (14.089 to 14.342 ms), so this is a memory reduction, not an established speedup. The 96-frame cafe sequence compared with the stationary-history baseline had mean absolute channel difference 0.000151 on the 0–255 scale and maximum 6, consistent with small intersection arithmetic differences. This retains the stationary shimmer fix. The final cafe image was visually inspected. The nine rendering scenarios completed with zero reported DirectX errors. Metropolis close measured 16.629 ms p95, overview 5.277 ms, and district 17.928 ms; the district still misses the 16.67 ms budget. Low, medium, unshadowed, edit-stress, and legacy execution checks completed.

## September 7 weathered roof tiles

Roof material 7 now combines its existing staggered tile joints with broad, world-fixed weathering, muted lichen colonies, and fine ceramic grain. Lichen slightly increases roughness; fine grain fades with pixel footprint. The treatment applies to both terracotta and slate palettes and introduces no assets or packages.

Release VoxelCity and VoxelGpuTests built; GPU conformance passed. Stationary 126-frame roof captures (`artifacts/roof-weather-before` and `artifacts/roof-weather-after`) and a 54-frame moving capture were inspected. Over the last 16 frames in the roof crop (x=450..1099, y=90..559), mean adjacent-frame RGB difference was 0.03370 before and 0.03380 after, with temporal standard deviation 0.09340 and 0.09402 on the 0–255 scale. The color change retains the existing stationary stability. Roof silhouettes, dormer repetition, and broader building variety still need work. The metropolis close and district checks completed with zero reported DirectX errors, at 17.059 and 17.760 ms p95 respectively; both miss the 16.67 ms target. Their average GPU times were 14.658 and 15.151 ms. The prior close GPU result was 14.342 ms; this art pass adds shader work and is retained for its visual change, not performance.

## September 7 pitched dormer caps

Variant 3 residential dormers now have shallow pitched metal caps, triangular front infill, and a raised ridge strip instead of a broad flat metal slab. The shape is authored in the existing voxel roof canvas; windows and their recesses remain open. The caps remain visibly stepped at close range.

The Release build and all eight tests passed, including building bounds, roof coverage in both orientations, and GPU conformance. The stationary 126-frame `artifacts/dormer-gables` and moving 54-frame `artifacts/dormer-gables-moving` captures completed successfully and their final images were visually inspected against `roof-weather-after`. The moving capture used the debug layer. This improves the paired dormer silhouette, but repetition across building variants remains a broader art gap. Metropolis close and district completed with zero reported DirectX errors, at 16.193 and 18.142 ms p95; average GPU times were 13.942 and 15.401 ms respectively. The 16.67 ms performance target remains unmet in the district view.

## September 7 residential side-yard furnishings

Plaster residential variants 1 and 3 now include a narrow timber-slatted bench on metal supports and a raised herb bed in the strip between the left hedge and wall. Separate leaf clusters expose soil between plants. Both props use the existing fine voxel canvas and material palette; no packages, gameplay occupancy, or parcel layout changed.

The Release build and all eight tests passed, including building bounds and GPU conformance. An elevated 54-frame `artifacts/house-yard-after` capture was inspected; it shows the bench, herb bed, hedge clearance, and rear fence. The original `house-yard-before` angle was largely occluded by the neighboring building and is not a matching image-comparison baseline. A 54-frame moving capture with the debug layer completed successfully. These details improve the residential grounds, while repeated parcel layouts remain a broader visual gap. The close and district metropolis checks reported zero DirectX errors and 17.418/18.445 ms p95, with 14.800/15.646 ms average GPU time. Packed voxel memory is 8.724 MiB (previously 8.705 MiB). The district performance target remains unmet.

## September 7 post-process edge smoothing

Added an edge-directed spatial filter to resolved lighting before bloom, atmosphere, and map overlays. Tone-mapped luminance detects high-contrast edges; low-contrast areas retain their original sample. Directional narrow/wide samples smooth stair-stepped raster edges, with local luminance limits rejecting excessive wide-filter blending. It adds no primary-ray jitter, frame-dependent sample sequence, history buffer, or package. Geometry voxel steps remain visible.

Release VoxelCity and VoxelGpuTests built, and GPU conformance passed. Matching 54-frame moving roof captures (`artifacts/edge-aa-before`, `edge-aa-after`) were inspected in the enlarged `edge-aa-comparison.png`: window surrounds and fascia edges are smoother, while the tile pattern remains readable. A stationary 126-frame capture and low/medium quality runs with the debug layer completed successfully. Close/district metropolis checks reported zero DirectX errors: p95 17.534/18.192 ms and average GPU 14.716/15.463 ms. Both remain above the 16.67 ms frame target. The prior pass measured GPU 14.800/15.646 ms, so these runs do not establish an additional performance penalty or speedup. This spatial filter does not reconstruct subpixel geometry and can soften fine high-contrast detail; full visual parity remains unproven.

## September 7 spreading broadleaf crowns

Broadleaf variants 0 and 1 now have wider limb reach, more widely offset forks, larger overlapping leaf sprays, and thinner primary supporting branches. The shorter variant has a broader spread than the tall variant. Roots, understory, conifers, placement, and clearing behavior retain their previous design. An initial flattened-leaf experiment (`artifacts/canopy-after`) was rejected because it made the trees look pruned and shelf-like; the retained `canopy-full` version restores fuller leaf volume.

Release build and all eight tests passed after the final revision, including quarter-turn placement clearance, vegetation clearing, and GPU conformance. Matching close-up `canopy-before` and `canopy-full` captures were visually inspected; the wider crown hides more of the thick branch junctions while preserving gaps. A moving capture with the debug layer completed successfully. The crown still uses repeated rounded leaf clusters, and the flat, sparse landscape remains a substantial gap from the reference. Close/district metropolis checks reported zero DirectX errors, p95 16.864/18.676 ms, and average GPU 14.031/15.928 ms. Packed voxel storage is 8.823 MiB, versus 8.724 MiB before this pass. The district still misses the 16.67 ms target.

## September 7 zero-weight history taps

Temporal resolve now computes each bilinear tap's weight before fetching and validating its previous position, normal, or light. Zero-weight taps are skipped. Exactly stationary static surfaces therefore fetch only their contributing pixel; moving surfaces retain all positive-weight reprojection taps. History limits and validation rules are unchanged.

Release VoxelCity and VoxelGpuTests built and GPU conformance passed. All 24 stationary canopy frames (`canopy-full` vs `history-taps-still`) and all 24 moving frames (`canopy-full-moving` vs `history-taps-moving`) were pixel-identical. The town benchmark's final captures differed around a moving car and are not used as an exact-image comparison. Town resolve time improved from 2.203 to 2.044 ms (7.2%), while total GPU time varied from 12.810 to 13.273 ms; this establishes a narrower resolve-stage improvement, not an overall speedup. The district check reported p95 18.526 ms, average GPU 15.617 ms, resolve 3.791 ms, and zero DirectX errors. The district performance target remains unmet.

## September 7 fallen branches beneath broadleaf trees

Broadleaf models now include a small fallen fork on the open side of their understory, with bark-covered limbs and exposed wood at the broken ends. The two variants differ in branch length and fork count. The prop shares the existing tree model, quarter-turn placement, and clearing lifetime; it introduces no separate scene instance or package.

Release build and all eight tests passed, including placement clearance, vegetation clearing, and GPU conformance. Ground-focused stationary and moving 54-frame captures (`artifacts/deadwood-close`, `deadwood-moving`) completed, and the static view was visually inspected: the branch sits among the fern beds beside the trunk. The moving capture used the debug layer. The scene still needs less repetitive vegetation placement and richer terrain composition to approach the reference. The close/district metropolis checks completed with zero DirectX errors and p95 15.968/18.904 ms, with average GPU 14.265/15.849 ms. Packed voxel storage is 8.828 MiB, compared with 8.823 MiB before this pass. Performance parity remains unfinished.

## September 7 constant spatial-filter kernels

Spatial reconstruction now uses precomputed Gaussian weights for its fixed 3x3 and 5x5 sample offsets. All neighbor tests, normal weighting, distance weighting, and filter support remain unchanged. The compiled resolve shader contains 35 Exp instructions instead of 43; eight exponentials of selected constants are removed.

Release VoxelCity and VoxelGpuTests built and GPU conformance passed. The 24 moving deadwood frames compared with the previous build changed only 141 color channels, each by one level out of 255 (mean absolute difference 0.00000136). District resolve measured 4.536 to 4.057 ms, while total GPU was nearly unchanged (16.263 to 16.193 ms) and p95 worsened from 18.164 to 19.057 ms. Retain as a resolve-stage reduction without claiming an overall frame-time improvement. The close view completed with p95 16.768 ms, GPU 15.179 ms, and resolve 2.541 ms. Both reports have zero DirectX errors. A plaster-house capture was also produced for visual inspection. The rendering budget remains unfinished.

## September 7 factory roller-shutter detail

Factory loading doors now use eighth-unit ribs at quarter-unit spacing, narrow metal guide tracks, rubber bottom seals and pull handles, and compact metal roller housings. This replaces the thick half-unit-spaced bars and concrete lintel blocks. A separate fine voxel canvas preserves the existing wall backing and merges its packed data with the factory model.

Release build and all eight tests passed, including factory roof coverage, building bounds, and GPU conformance. Matching 54-frame factory views (`artifacts/factory-review-before` and `factory-shutters`) were visually inspected; the doors read more clearly as roller shutters. A moving 54-frame capture with the debug layer completed successfully. Industrial building repetition and the simplified roof glazing remain visible gaps. Close/district metropolis reports recorded zero DirectX errors, p95 17.216/18.612 ms, and GPU 15.306/16.291 ms. Packed voxel memory is 8.920 MiB. The district frame budget remains unmet.

## September 7 factory ventilation intake

The factory side intake now has a dark recessed face, five shallow metal louvers, and a narrow perimeter frame, replacing three thick rubber blocks. An eighth-unit fixture canvas merges alongside the shutter detail and retains the original metal wall housing.

Release build and all eight tests passed. A local variable was subsequently renamed to remove a shadowing warning; VoxelCity and VoxelGpuTests rebuilt successfully. Elevated `factory-intake-before`/`factory-intake-after` views and the clearer `factory-intake-side` view were captured; the service-side image was visually inspected. The original `factory-vent-before` angle was obstructed by a neighboring roof and is not a usable baseline. A moving service-side capture with the debug layer completed and was visually inspected. The district benchmark reported p95 18.359 ms, average GPU 16.048 ms, packed voxel memory 8.933 MiB, and zero DirectX errors. The frame budget and full visual parity remain unfinished.

## September 7 rejected glass-filter falloff experiment

Tested a gentler Gaussian falloff within glass's existing 3x3 spatial filter, preserving sample count, support, rejection, and temporal limits. Enlarged moving factory and shop comparisons showed only modestly softer mottling, with shelves and frames still readable. The candidate built and passed GPU conformance.

The candidate district benchmark measured 20.287 ms p95 and 17.080 ms average GPU, versus 18.359 and 16.048 ms in the preceding check. Close measured 17.189 ms p95 and 15.099 ms GPU. Both reports had zero DirectX errors, but the small visual gain did not justify the observed district slowdown. This is not a controlled proof that all of the timing difference was caused by the filter. Rejected the candidate and restored the exact preceding shader source from `artifacts/glass-filter-baseline.hlsl`. The candidate and captures remain in artifacts for evidence; `glass-filter-close/district` reports describe the rejected build. The restored Release build and GPU conformance test passed; shader text matches the saved baseline exactly. Moving-glass noise still needs a more effective solution.

## September 7 factory roof-access ladder

The factory ladder now uses eighth-unit rails and rungs, a narrower span, closer rung spacing, and a lower first step. Metal stand-offs connect it to the wall, and the rails extend above the roof as handholds. Its separate fine canvas merges with the existing factory fixtures.

Release build and all eight tests passed, including building bounds and GPU conformance. Matching service-side images (`factory-intake-side` before and `factory-ladder` after) and a moving `factory-ladder-moving` capture were visually inspected. The rungs remain readable in the moving view. The moving capture used the debug layer. District reported p95 18.459 ms, average GPU 16.106 ms, packed voxel storage 8.952 MiB, and zero DirectX errors. This improves the scale of one service fitting; building variety, lighting, and the frame budget remain broader gaps.

## September 7 factor target color out of spatial filtering

Matte-surface reconstruction now divides each neighbor's radiance by that neighbor's material color, accumulates illumination, and applies the target pixel's color once after normalization. Previously the target color was multiplied into every accepted tap. Filter support, weights, rejection, and fallback shading remain unchanged.

Release VoxelCity and VoxelGpuTests built and GPU conformance passed. Across 24 matching moving house frames (`filter-color-before`/`after`), only 1,420 color channels changed, with maximum difference 3 and mean absolute difference 0.00001379 on the 0–255 scale. The final image was visually inspected and retained the surface detail. The capture used the debug layer. District resolve measured 4.098 to 3.870 ms (5.6% lower), average GPU 16.095 to 15.928 ms, and p95 18.409 to 18.537 ms. Both reports had zero DirectX errors. Retain as a small filter optimization, not a proven overall frame-time improvement; the frame budget and visual parity remain unfinished.

## September 7 stones in scrub patches

Scrub variant 4 now includes three low stones with unequal dimensions and asymmetrically clipped voxel facets. Their bases intersect the ground plane, and some foliage overlaps their edges. The new VStone palette entry (26) uses a rough, nonmetallic gray surface with continuous mineral mottling, footprint-filtered grain, and a darker ground contact region. The stones share the scrub model's placement and clearing lifetime; no package or separate scene instance was added.

Release build and all eight tests passed, including vegetation bounds/clearing and GPU conformance with the new material. Matching stationary `scrub-stone-before`/`after` and a moving `scrub-stone-moving` capture were visually inspected. The stones add a low gray accent among the plants without dominating the patch. The moving capture used the debug layer. District reported p95 19.310 ms, average GPU 16.477 ms, packed voxel storage 8.955 MiB, and zero DirectX errors. The repeated scrub layout, flat terrain, and unmet frame budget remain significant gaps.

## September 7 recessed factory chimney flue

The factory chimney now has a half-unit flue cut through its metal cap and into the brick shaft, ending in a recessed dark base. The surrounding cap forms a thick rim. This adds actual cavity depth to the previously solid slab.

Release build and all eight tests passed. Stationary and moving service-side captures (`factory-flue`, `factory-flue-moving`) were visually inspected; the brick-lined opening remains readable in motion. The moving capture used the debug layer. District reported p95 18.625 ms, average GPU 16.170 ms, packed voxel storage 8.955 MiB, and zero DirectX errors. This is a small detail improvement; repeated buildings, sparse landscape composition, moving-glass noise, and the rendering budget remain broader gaps.

## Remaining visual gaps

Parity is not achieved. Building silhouettes are more varied, but most apartment
and commercial facades still share the same window grid. Streets and lots need more
authored detail and layered vegetation. Glass transmission and room illumination
are approximate, and lighting has only one diffuse bounce with limited reflection
distance. Roof lighting is better
reconstructed, but quarter-unit stair geometry remains conspicuous at close range,
and distant subpixel silhouettes remain limited despite spatial edge smoothing.

Next comparisons should cover architectural variety and roof self-shadow quality,
then foliage at close range, reflective surfaces and scene atmosphere. Judge each
pass with actual stationary and moving captures, and keep the stationary-edge fix
and the normal-city 16.67 ms rendering budget throughout.
