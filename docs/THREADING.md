# Thread ownership and validation

## Ownership

- Main thread: Win32 message pump, camera, ImGui context and Win32 backend, authoritative World and CitySimulation, vehicle movement, trip ownership, spawning and completion events.
- RenderWorker: Renderer, both rendering pipelines, DX12 UI backend, swapchain, GPU resource lifetimes, resizing and captures. A named `VoxelCity Rendering` thread receives owned packets. Existing legacy mesh jobs read private world copies.
- NetworkWorker: one named `VoxelCity Pathfinding` thread shared by traffic and coverage. All graph traversals and route expansions execute here. Ownership checks reject accidental execution on the main thread.

Workers do not capture live simulation objects. Futures publish completed results; mutexes and condition variables protect queues, statistics and shutdown. The network worker sleeps when idle. The main thread remains responsible for applying results and never waits on ordinary gameplay searches.

## Rendering handoff

Packets copy vehicle transforms and UI vertices, indices and commands. World visuals are copied only on topology, parcel, style or replacement changes. The worker computes dirty chunks against its last consumed snapshot, including neighbor chunks, so dropped intermediate frames cannot lose edits. Explicit city replacement resets renderer caches even if a loaded city's revision numbers match the prior city.

There is one in-progress frame and one pending frame. A newer ordinary frame replaces the pending frame. Captures are reliable and drained, and automated frame-limited runs explicitly wait after submission. `--threading-test` instead delays the renderer by 40 ms and runs the existing city input script without a frame barrier; it asserts that input advances and pending frames are replaced.

`UiRenderer.cpp` is an application-owned adaptation of the Dear ImGui 1.91.9b MIT DX12 backend. It renders plain copied buffers and uses thread-local backend state rather than ImGui's live context. The font atlas is built on the main thread before coordinated GPU initialization; there are no runtime font changes. Arbitrary UI draw callbacks are rejected; reset-render-state commands are supported. Backend allocations use native allocation, avoiding ImGui's context-based debug allocation tracking.

Interactive main-loop pacing is capped near 120 Hz when vsync is enabled. Waiting for render initialization, reliable captures and shutdown continues to pump Windows messages. Renderer destruction and GPU retirement happen before the window/context are destroyed.

## Network work and persistence

The network queue holds at most 64 jobs. Producers retain their logical requests and retry when it is full. A traffic batch holds at most 256 requests and reuses one search scratch buffer. Each job executes at most the configured route expansion budget (normally 20,000), yielding between slices. Coverage is split into connectivity preparation and one facility per job, interleaving with route jobs. Graph preparation has a fixed 512-by-512 map bound.

Traffic topology jobs carry topology and replacement revisions. Each route batch retains its immutable graph snapshot; obsolete batches are cancelled and owned requests are returned to the main-thread queue. Repair results also validate vehicle serial identity, current tile and repair state before installation. City coverage additionally checks an access revision advanced by building/facility edits. New work is appended fairly to the shared queue; repair requests precede new trips within a traffic batch.

While a new topology is pending, vehicle motion is held, removed-road vehicles are removed safely, and surviving vehicles are flagged for repair. Coverage-dependent city updates wait for current access data; paused cities still poll and publish coverage. Snapshot timing can change route selection and completion ticks, but direction, diagonal, highway and roundabout rules are preserved.

Traffic payload version 3 stores pending logical requests and repair flags rather than worker-owned search scratch. Versions 1 and 2 are parsed and validated, then their unfinished searches restart. Saving does not wait on route or graph completion. Loading validates a candidate and constructs its initial graph on the worker before replacing the city. Startup and headless correctness tests use an explicit blocking drain mode; this still performs all searches on the worker.

## Validation

`ThreadingTests` covers worker ownership, nonblocking simulation and saves under a deliberately stalled worker, cancelled/stale routes, pending-trip reload, destruction during work, queue saturation, asynchronous coverage, and dirty chunks across skipped frames. Existing correctness tests retain deterministic worker barriers. No ThreadSanitizer result is claimed on this Windows/MSVC build.

`scripts/verify-threading.ps1` runs the headless threading tests, delayed voxel and legacy input, regular input, resizing/minimize/restore, road edits, and reliable capture sequences. Optional `-Benchmarks` adds the metropolis and 5,000-vehicle scenarios at 1x and 3x speed. Reports retain end-to-end frame timing and add actual presentation time, main-thread work, render-worker time, snapshot handoff cost, route latency and queue depth.

### Measured Release results (RTX 4070, 2560 x 1440)

The 630-frame checks preserved 10,000 residents and reported no DirectX errors.

| Scenario | Main-thread work average | Presentation p95 | Minimum active vehicles |
| --- | ---: | ---: | ---: |
| Metropolis | 0.588 ms | 6.447 ms | 471 |
| 5,000-vehicle stress | 1.395 ms | 6.595 ms | 5,044 |
| Stress at 3x simulation | 2.639 ms | 7.133 ms | 5,011 |

The historical `artifacts/city-stress.json` report recorded 4.062 ms traffic-tick p95; the threaded run recorded 1.531 ms on the main thread, with routing accounted for separately on the worker. Overall frame p95 remained similar. These are historical comparisons, not a controlled claim of increased GPU frame rate. Automated benchmark runs deliberately drain each frame and therefore do not measure the full overlap available during ordinary gameplay.

Both delayed-render checks advanced the complete 90-frame city input script while replacing 88 pending frames, then captured the final world correctly. The Release seven-test suite and Debug threading/ownership checks passed. No long-duration soak or CPU race-detector result is claimed.
