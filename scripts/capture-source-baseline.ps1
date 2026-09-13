param([string]$Name = ('source-baseline-' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
$ErrorActionPreference = 'Stop'
if ($Name -notmatch '^[a-zA-Z0-9_-]+$') { throw 'Name must contain only letters, digits, hyphens or underscores.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$destination = Join-Path $projectRoot "artifacts/$Name"
if (Test-Path -LiteralPath $destination) { throw "Baseline already exists: $destination" }
Push-Location $projectRoot
try {
    $revision = & git rev-parse HEAD
    if ($LASTEXITCODE) { throw 'Could not read source revision.' }
    $status = & git status --short
    if ($LASTEXITCODE) { throw 'Could not read working-tree status.' }
    $files = & git -c core.quotepath=false ls-files --cached --others --exclude-standard
    if ($LASTEXITCODE) { throw 'Could not enumerate source files.' }
    $files = @($files | Sort-Object -Unique | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    New-Item -ItemType Directory -Path $destination | Out-Null
    $zipPath = Join-Path $destination 'source.zip'
    $zip = [System.IO.Compression.ZipFile]::Open($zipPath, 'Create')
    try {
        foreach ($file in $files) {
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $projectRoot $file), $file.Replace('\', '/')) | Out-Null
        }
    } finally { $zip.Dispose() }
    $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $manifest = foreach ($entry in $zip.Entries) {
            # Hash the archived bytes, so the manifest describes the actual snapshot.
            $stream = $entry.Open()
            try {
                $sha = [System.Security.Cryptography.SHA256]::Create()
                try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') } finally { $sha.Dispose() }
            } finally { $stream.Dispose() }
            [pscustomobject]@{ Path = $entry.FullName; SHA256 = $hash }
        }
    } finally { $zip.Dispose() }
    [pscustomobject]@{
        CapturedAt = (Get-Date).ToString('o')
        Revision = $revision
        Status = @($status)
        Files = @($manifest)
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'manifest.json') -Encoding utf8
    Get-FileHash -LiteralPath (Join-Path $destination 'source.zip') -Algorithm SHA256 |
        Format-List | Out-String | Set-Content -LiteralPath (Join-Path $destination 'source.sha256.txt')
    Write-Output $destination
} finally { Pop-Location }
