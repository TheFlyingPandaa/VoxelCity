param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot "build\$Configuration\VoxelCity.exe"
$tests = Join-Path $projectRoot "build\$Configuration\TrafficTests.exe"
$artifactPath = Join-Path $projectRoot 'artifacts'
New-Item -ItemType Directory -Path $artifactPath -Force | Out-Null
Push-Location $projectRoot
try {
    & $tests
    if ($LASTEXITCODE) { throw 'Traffic correctness tests failed.' }
    foreach ($count in @(50000,100000)) {
        & $tests $count | Tee-Object -FilePath "artifacts/traffic-headless-$count.txt"
        if ($LASTEXITCODE) { throw "Headless traffic benchmark failed at $count cars." }
    }
    $checks = @(
        @{Name='traffic-closeup'; Args='--scenario showcase --cars 100 --close-up --frames 300 --debug-layer'},
        @{Name='traffic-editing'; Args='--scenario network --cars 5000 --edit-stress --frames 600 --debug-layer --window-test'},
        @{Name='traffic-50k-close'; Args='--benchmark --scenario full --cars 50000 --traffic-warmup 1000 --frames 630'; Target=50000},
        @{Name='traffic-50k-overview'; Args='--benchmark --scenario full --cars 50000 --traffic-warmup 1000 --frames 630 --overview'; Target=50000},
        @{Name='traffic-100k-overview'; Args='--benchmark --scenario full --cars 100000 --traffic-warmup 2400 --frames 630 --overview'; Target=100000}
    )
    foreach ($check in $checks) {
        $arguments = "$($check.Args) --traffic-benchmark --traffic-seed 42 --report artifacts/$($check.Name).json --capture artifacts/$($check.Name).bmp"
        $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -Wait
        if ($process.ExitCode -ne 0) { throw "$($check.Name) failed with exit code $($process.ExitCode)." }
        $report = Get-Content -LiteralPath "artifacts/$($check.Name).json" -Raw | ConvertFrom-Json
        if ($report.debug_errors -ne 0) { throw "DirectX errors in $($check.Name)." }
        if ($check.Target -and ($report.max_cars -lt $check.Target -or $report.min_cars -lt $check.Target - 8)) { throw "$($check.Name) did not sustain the requested population." }
        [pscustomobject]@{Check=$check.Name; Cars=$report.cars; FrameP95=$report.p95_frame_ms; SimulationP95=$report.traffic_p95_ms; GpuMs=$report.average_gpu_ms}
    }
} finally { Pop-Location }
