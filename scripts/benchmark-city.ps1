param([int[]]$Population = @(10000,25000), [int]$Ticks = 900, [int]$Warmup = 300, [int]$Runs = 3)
$ErrorActionPreference = 'Stop'
if ($Ticks -lt 1 -or $Warmup -lt 0 -or $Runs -lt 1) { throw 'Invalid benchmark duration or run count.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$output = Join-Path $projectRoot ('artifacts/perf-01-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $output | Out-Null
Push-Location $projectRoot
try {
    $metadata = [ordered]@{
        timestamp = (Get-Date).ToUniversalTime().ToString('o')
        revision = (& git rev-parse HEAD)
        workingTree = @(& git status --short)
        cpu = (Get-ItemProperty 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0' -Name ProcessorNameString).ProcessorNameString
        configuration = 'Release'; workerMode = 'blocking'; rendered = $false
        executableHash = (Get-FileHash ./build/Release/CityBenchmark.exe -Algorithm SHA256).Hash
        sourceHashes = @{}
    }
    foreach ($file in @(& git ls-files src tests CMakeLists.txt)) {
        $metadata.sourceHashes[$file] = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    }
    # Include the new benchmark before it has been added to Git.
    $metadata.sourceHashes['tests/CityBenchmark.cpp'] = (Get-FileHash tests/CityBenchmark.cpp).Hash
    $metadata | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $output 'environment.json')
    $missed = $false
    foreach ($target in $Population) {
        for ($run = 1; $run -le $Runs; $run++) {
            $report = & ./build/Release/CityBenchmark.exe $target $Ticks $Warmup
            $code = $LASTEXITCODE
            $report | Set-Content (Join-Path $output "$target-$run.json")
            if ($code -eq 2) { $missed = $true }
            elseif ($code -ne 0) { throw "Benchmark failed for $target with exit code $code" }
            $parsed = $report | ConvertFrom-Json
            [pscustomobject]@{Target=$target; Actual=$parsed.minimum_population; Run=$run; TicksPerSecond=$parsed.ticks_per_wall_second; P95Ms=$parsed.tick_p95_ms}
        }
    }
    Write-Host "Reports: $output"
    if ($missed) { throw 'One or more population targets were missed; reports retained.' }
} finally { Pop-Location }
