param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$executable=Join-Path $projectRoot "build\$Configuration\VoxelCity.exe"
Push-Location $projectRoot
try {
    & (Join-Path $projectRoot "build\$Configuration\RailwayTests.exe")
    if ($LASTEXITCODE) { throw 'Railway correctness tests failed.' }
    foreach ($renderer in @('voxel','legacy')) {
        $arguments="--railway-scenario --traffic-seed 42 --traffic-warmup 2300 --traffic-benchmark --frames 90 --renderer $renderer --debug-layer --no-grid --capture artifacts/railway-$renderer.bmp --report artifacts/railway-$renderer.json"
        $process=Start-Process -FilePath $executable -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait
        if ($process.ExitCode) { throw "$renderer railway rendering failed." }
        $report=Get-Content -LiteralPath "artifacts/railway-$renderer.json" -Raw | ConvertFrom-Json
        if ($report.debug_errors -ne 0 -or $report.rail_trains -lt 1) { throw "$renderer railway validation failed." }
        [pscustomobject]@{Renderer=$renderer;Trains=$report.rail_trains;P95Ms=$report.p95_frame_ms;DebugErrors=$report.debug_errors}
    }
} finally { Pop-Location }
