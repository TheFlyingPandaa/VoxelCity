param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot "build\$Configuration\VoxelCity.exe"
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'artifacts') -Force | Out-Null
Push-Location $projectRoot
try {
    $checks = @(
        @{Name='voxel-edit-release'; Args='--benchmark --scenario network --cars 0 --edit-stress --frames 300 --debug-layer'; Budget=16.67},
        @{Name='voxel-metropolis-close'; Args='--benchmark --city-scenario metropolis --traffic-warmup 1800 --frames 630 --traffic-benchmark --close-up --camera-path --no-ui'; Budget=16.67; Population=10000},
        @{Name='voxel-metropolis-overview'; Args='--benchmark --city-scenario metropolis --traffic-warmup 1800 --frames 630 --traffic-benchmark --overview --camera-path --no-ui'; Budget=16.67; Population=10000},
        @{Name='voxel-metropolis'; Args='--benchmark --city-scenario metropolis --traffic-warmup 1800 --frames 630 --traffic-benchmark --camera-path'; Budget=16.67; Population=10000},
        @{Name='voxel-close'; Args='--benchmark --city-scenario town --frames 180 --close-up --no-ui'},
        @{Name='voxel-unshadowed'; Args='--benchmark --city-scenario town --frames 180 --close-up --no-ui --no-shadows'},
        @{Name='voxel-low'; Args='--city-scenario town --frames 90 --close-up --quality low --debug-layer'},
        @{Name='voxel-medium'; Args='--city-scenario town --frames 90 --close-up --quality medium --debug-layer'},
        @{Name='voxel-legacy'; Args='--city-scenario town --frames 90 --close-up --renderer legacy --debug-layer'}
    )
    foreach ($check in $checks) {
        $arguments = "$($check.Args) --traffic-seed 42 --report artifacts/$($check.Name).json --capture artifacts/$($check.Name).bmp"
        $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -Wait
        if ($process.ExitCode -ne 0) { throw "$($check.Name) failed with exit code $($process.ExitCode)." }
        $report = Get-Content -LiteralPath "artifacts/$($check.Name).json" -Raw | ConvertFrom-Json
        if ($report.debug_errors -ne 0) { throw "DirectX errors in $($check.Name)." }
        if ($check.Population -and $report.city_population -lt $check.Population) { throw 'Population target missed.' }
        if ($Configuration -eq 'Release' -and $check.Budget -and $report.p95_frame_ms -gt $check.Budget) { throw "$($check.Name) missed its 60 FPS budget." }
        [pscustomobject]@{Check=$check.Name; P95Ms=$report.p95_frame_ms; GpuMs=$report.average_gpu_ms; PeakMiB=$report.peak_video_memory_mib; Errors=$report.debug_errors}
    }
} finally { Pop-Location }
