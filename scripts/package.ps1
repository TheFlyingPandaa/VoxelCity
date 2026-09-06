param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release }
$source = Join-Path $projectRoot 'build\Release'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$destination = Join-Path $projectRoot "artifacts\release\VoxelCity-0.3.0-$stamp"
New-Item -ItemType Directory -Path $destination -Force | Out-Null
foreach ($file in @('VoxelCity.exe','DearImGui-LICENSE.txt')) { Copy-Item -LiteralPath (Join-Path $source $file) -Destination $destination }
foreach ($folder in @('shaders','data')) { Copy-Item -LiteralPath (Join-Path $source $folder) -Destination $destination -Recurse }
foreach ($file in @('README.md','THIRD_PARTY.md')) { Copy-Item -LiteralPath (Join-Path $projectRoot $file) -Destination $destination }
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs') -Destination $destination -Recurse
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\CITY_QUICKSTART.md') -Destination (Join-Path $destination 'QUICKSTART.md')
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$runtimeFound = $false
if (Test-Path -LiteralPath $vswhere) {
    foreach ($installation in (& $vswhere -all -products '*' -property installationPath)) {
        $redist = Join-Path $installation 'VC\Redist\MSVC'
        if (!(Test-Path -LiteralPath $redist)) { continue }
        foreach ($version in (Get-ChildItem -LiteralPath $redist -Directory | Where-Object Name -Match '^\d+\.' | Sort-Object { [version]$_.Name } -Descending)) {
            $crt = Join-Path $version.FullName 'x64\Microsoft.VC143.CRT'
            if (Test-Path -LiteralPath $crt) {
                Get-ChildItem -LiteralPath $crt -Filter '*.dll' | Copy-Item -Destination $destination
                $runtimeFound = $true
                break
            }
        }
        if ($runtimeFound) { break }
    }
}
if (!$runtimeFound) { throw 'VC143 redistributable runtime not found; install Visual Studio C++ redistributable tools before packaging.' }
$archive = "$destination.zip"
Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $archive
Write-Output $destination
Write-Output $archive
