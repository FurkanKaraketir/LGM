param(
    [string]$Version = "0.1.0",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $root $BuildDir
$dist = Join-Path $root "dist"
$stage = Join-Path $dist "LGM-$Version-win64"

if (-not (Test-Path (Join-Path $buildPath "LGM.exe"))) {
    throw "LGM.exe not found in $buildPath — build first."
}

New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item (Join-Path $buildPath "LGM.exe") $stage
Get-ChildItem $buildPath -Filter "*.dll" | Copy-Item -Destination $stage
if (Test-Path (Join-Path $buildPath "Examples")) {
    Copy-Item -Recurse -Force (Join-Path $buildPath "Examples") (Join-Path $stage "Examples")
}

New-Item -ItemType Directory -Force -Path $dist | Out-Null
$zip = Join-Path $dist "LGM-$Version-win64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Created $zip"
