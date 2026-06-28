param(
    [string]$Version = "0.2.0",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $root $BuildDir
$dist = Join-Path $root "dist"
$stage = Join-Path $dist "LGM-$Version-win64"
$iss = Join-Path $PSScriptRoot "installer\LGM.iss"

if (-not (Test-Path (Join-Path $buildPath "LGM.exe"))) {
    throw "LGM.exe not found in $buildPath - build first."
}

if (Test-Path $stage) {
    Remove-Item -Recurse -Force $stage
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item (Join-Path $buildPath "LGM.exe") $stage -Force
Get-ChildItem $buildPath -Filter "*.dll" | Copy-Item -Destination $stage -Force
$platforms = Join-Path $buildPath "platforms"
if (Test-Path $platforms) {
    Copy-Item -Recurse -Force $platforms (Join-Path $stage "platforms")
}
foreach ($dir in @("styles", "imageformats", "iconengines", "tls", "networkinformation", "generic")) {
    $src = Join-Path $buildPath $dir
    if (Test-Path $src) {
        Copy-Item -Recurse -Force $src (Join-Path $stage $dir)
    }
}
if (Test-Path (Join-Path $buildPath "Examples")) {
    Copy-Item -Recurse -Force (Join-Path $buildPath "Examples") (Join-Path $stage "Examples")
}

New-Item -ItemType Directory -Force -Path $dist | Out-Null

$zip = Join-Path $dist "LGM-$Version-win64-portable.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Created $zip"

$isccCandidates = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($iscc) {
    & $iscc "/DAppVersion=$Version" "/DStageDir=$stage" "/DRepoRoot=$root" $iss
    $setup = Join-Path $dist "LGM-$Version-win64-setup.exe"
    if (-not (Test-Path $setup)) {
        throw "Inno Setup finished but $setup was not created."
    }
    Write-Host "Created $setup"
} else {
    Write-Warning "Inno Setup 6 not found - portable zip only. Install from https://jrsoftware.org/isinfo.php"
}

Remove-Item -Recurse -Force $stage
