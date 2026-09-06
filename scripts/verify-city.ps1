param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$Growth)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot "build\$Configuration\VoxelCity.exe"
$tests = Join-Path $projectRoot "build\$Configuration\CityTests.exe"
Push-Location $projectRoot
try {
    & $tests
    if ($LASTEXITCODE) { throw 'City correctness tests failed.' }
    if ($Growth) {
        & $tests growth | Tee-Object -FilePath artifacts/city-growth.txt
        if ($LASTEXITCODE) { throw 'From-empty city growth test failed.' }
    }
    $checks = @(
        @{Name='city-input'; Args='--city-input-test --debug-layer'},
        @{Name='city-town'; Args='--city-scenario town --frames 180 --window-test --debug-layer'},
        @{Name='city-metropolis'; Args='--benchmark --city-scenario metropolis --traffic-warmup 1800 --frames 630 --traffic-benchmark --debug-layer'; Population=10000},
        @{Name='city-stress'; Args='--benchmark --city-stress 5000 --traffic-benchmark --frames 630'; Population=10000; Vehicles=5000; FrameBudget=16.67},
        @{Name='city-stress-3x'; Args='--benchmark --city-stress 5000 --traffic-benchmark --city-speed 3 --frames 630'; Population=10000; Vehicles=5000; FrameBudget=33.3}
    )
    foreach ($check in $checks) {
        $arguments = "$($check.Args) --traffic-seed 42 --report artifacts/$($check.Name).json --capture artifacts/$($check.Name).bmp"
        $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -Wait
        if ($process.ExitCode -ne 0) { throw "$($check.Name) failed with exit code $($process.ExitCode)." }
        $report = Get-Content -LiteralPath "artifacts/$($check.Name).json" -Raw | ConvertFrom-Json
        if ($report.debug_errors -ne 0) { throw "DirectX errors in $($check.Name)." }
        if ($check.Population -and $report.city_population -lt $check.Population) { throw "Population target missed in $($check.Name)." }
        if ($check.Vehicles -and $report.min_cars -lt $check.Vehicles) { throw "Actual active-vehicle target missed in $($check.Name)." }
        if ($Configuration -eq 'Release' -and $check.FrameBudget -and $report.p95_frame_ms -gt $check.FrameBudget) { throw "Frame budget missed in $($check.Name)." }
        [pscustomobject]@{Check=$check.Name; Residents=$report.city_population; MinVehicles=$report.min_cars; P95Ms=$report.p95_frame_ms; CityTickMs=$report.city_tick_ms}
    }
} finally { Pop-Location }
