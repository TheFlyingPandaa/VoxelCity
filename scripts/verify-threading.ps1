param([ValidateSet('Debug','Release')][string]$Configuration='Release',[switch]$Benchmarks)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$executable=Join-Path $projectRoot "build\$Configuration\VoxelCity.exe"
& (Join-Path $projectRoot "build\$Configuration\ThreadingTests.exe")
if($LASTEXITCODE){throw 'Threading correctness checks failed.'}
$checks=@(
    @{Name='delayed';Args='--threading-test --debug-layer'},
    @{Name='delayed-legacy';Args='--threading-test --renderer legacy --debug-layer'},
    @{Name='input';Args='--city-input-test --debug-layer'},
    @{Name='resize';Args='--city-scenario town --frames 180 --window-test --debug-layer'},
    @{Name='edits';Args='--scenario diagonal --cars 100 --frames 180 --edit-stress --debug-layer'},
    @{Name='sequence';Args='--scenario empty --cars 0 --frames 36 --capture-sequence artifacts/threaded-sequence --debug-layer'}
)
if($Benchmarks){$checks+=@(
    @{Name='metropolis';Args='--benchmark --city-scenario metropolis --traffic-warmup 1800 --frames 630 --traffic-benchmark --debug-layer';Population=10000},
    @{Name='stress';Args='--benchmark --city-stress 5000 --frames 630 --traffic-benchmark';Vehicles=5000;Population=10000},
    @{Name='stress-3x';Args='--benchmark --city-stress 5000 --city-speed 3 --frames 630 --traffic-benchmark';Vehicles=5000;Population=10000}
)}
Push-Location $projectRoot
try {
    foreach($check in $checks){
        $arguments="$($check.Args) --traffic-seed 42 --report artifacts/threaded-$($check.Name).json --capture artifacts/threaded-$($check.Name).bmp"
        $process=Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -Wait
        if($process.ExitCode){throw "$($check.Name) failed with exit code $($process.ExitCode)."}
        $report=Get-Content "artifacts/threaded-$($check.Name).json" -Raw | ConvertFrom-Json
        if($report.debug_errors){throw "DirectX errors in $($check.Name)."}
        if($check.Vehicles -and $report.min_cars -lt $check.Vehicles){throw 'Active vehicle target missed.'}
        if($check.Population -and $report.city_population -lt $check.Population){throw 'Population target missed.'}
        if($check.Name -like 'delayed*' -and $report.replaced_frames -lt 1){throw 'Delayed render did not exercise backpressure.'}
        [pscustomobject]@{Check=$check.Name;P95Ms=$report.presentation_p95_ms;MainMs=$report.main_thread_work_average_ms;RenderMs=$report.render_worker_average_ms;RouteLatencyMs=$report.route_latency_ms;MinVehicles=$report.min_cars;Errors=$report.debug_errors}
    }
    foreach($frame in 0..5){if(!(Test-Path ("artifacts/threaded-sequence/frame-{0:D5}.bmp" -f $frame))){throw 'Capture sequence dropped a frame.'}}
} finally {Pop-Location}
