# Regenerate golden CLI benchmarks for the example corpus.
# Run from repository root: .\docs\publication\benchmarks\generate.ps1

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path

$lgm = Join-Path $root "build\LGM.exe"
if (-not (Test-Path $lgm)) {
    $lgm = Join-Path $root "build\LGM"
}
if (-not (Test-Path $lgm)) {
    throw "Build LGM first: cmake --build build"
}

$outDir = Join-Path $PSScriptRoot "golden"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$examples = [ordered]@{
    Motor              = @("T_J", "i_L", "T_B")
    Electrical         = @()
    Gyrator            = @()
    TransformerCascade = @()
    PumpAccumulator    = @()
    FloatingSource     = @()
    MultiNormalTree    = @()
}

foreach ($name in $examples.Keys) {
    $path = Join-Path $root "Examples\$name.lgm"
    if (-not (Test-Path $path)) {
        Write-Warning "Skip missing $path"
        continue
    }
    $args = @("--analyze", $path) + $examples[$name]
    Write-Host "Analyzing $name ..."
    $argStr = ($args | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '
    cmd /c "`"$lgm`" $argStr 2>nul" | Out-File -FilePath (Join-Path $outDir "$name.txt") -Encoding utf8
}

Write-Host "Wrote $($examples.Count) files to $outDir"
