param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot "build\$Configuration\VoxelCity.exe"
if (!(Test-Path -LiteralPath $executable)) { throw 'Build the application first with scripts/build.ps1.' }
$artifactPath = Join-Path $projectRoot 'artifacts'
New-Item -ItemType Directory -Path $artifactPath -Force | Out-Null
Push-Location $projectRoot
try {
    $checks = @(
        @{Name='input'; Arguments='--interaction-test --debug-layer'},
        @{Name='resize'; Arguments='--smoke-test --window-test'},
        @{Name='empty'; Arguments='--benchmark --frames 3000 --scenario empty'},
        @{Name='network'; Arguments='--benchmark --frames 3000 --scenario network'},
        @{Name='full'; Arguments='--benchmark --frames 3000 --scenario full'},
        @{Name='overview'; Arguments='--benchmark --frames 3000 --scenario network --overview'},
        @{Name='editing'; Arguments='--benchmark --frames 600 --scenario network --edit-stress --debug-layer'},
        @{Name='showcase'; Arguments='--frames 90 --scenario showcase --debug-layer'},
        @{Name='curbs'; Arguments='--frames 90 --scenario showcase --close-up'},
        @{Name='unshadowed'; Arguments='--frames 90 --scenario showcase --close-up --no-shadows'}
    )
    foreach ($check in $checks) {
        $arguments = "$($check.Arguments) --cars 0 --report artifacts/$($check.Name).json --capture artifacts/$($check.Name).bmp"
        $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -Wait
        if ($process.ExitCode -ne 0) { throw "$($check.Name) failed with exit code $($process.ExitCode). See voxelcity-error.log and d3d12-debug.log." }
        $report = Get-Content -LiteralPath "artifacts/$($check.Name).json" -Raw | ConvertFrom-Json
        if ($report.debug_errors -ne 0) { throw "DirectX validation errors in $($check.Name)." }
        [pscustomobject]@{Check=$check.Name; FrameMs=$report.average_frame_ms; P95Ms=$report.p95_frame_ms; GpuMs=$report.average_gpu_ms; MemoryMiB=$report.video_memory_mib}
    }
} finally { Pop-Location }
