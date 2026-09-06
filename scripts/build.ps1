param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$Run
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$candidates = @()
$onPath = Get-Command cmake -ErrorAction SilentlyContinue
if ($onPath) { $candidates += $onPath.Source }
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $installations = & $vswhere -all -products '*' -property installationPath
    foreach ($installation in $installations) {
        $candidates += Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }
}
$cmakePath = $null
foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate) {
        $versionText = & $candidate --version
        if ($versionText[0] -match '(\d+\.\d+\.\d+)' -and [version]$Matches[1] -ge [version]'3.24.0') {
            $cmakePath = $candidate
            break
        }
    }
}
if (!$cmakePath) { throw 'CMake 3.24+ is required. Install the Visual Studio C++ CMake tools component.' }
Push-Location $projectRoot
try {
    & $cmakePath --preset windows
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    & $cmakePath --build --preset $Configuration.ToLowerInvariant() --parallel
    if ($LASTEXITCODE) { throw 'Build failed.' }
    $ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
    & $ctestPath --preset $Configuration.ToLowerInvariant()
    if ($LASTEXITCODE) { throw 'World tests failed.' }
    if ($Run) { & (Join-Path $projectRoot "build\$Configuration\VoxelCity.exe") }
} finally { Pop-Location }
