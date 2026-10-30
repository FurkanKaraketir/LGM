# Compare LGM --analyze output to golden CLI benchmarks.
# Run from repository root: .\docs\publication\benchmarks\check.ps1

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path

$lgm = Join-Path $root "build\LGM.exe"
if (-not (Test-Path $lgm)) {
    $lgm = Join-Path $root "build\LGM"
}
if (-not (Test-Path $lgm)) {
    throw "Build LGM first: cmake --build build"
}

$goldenDir = Join-Path $PSScriptRoot "golden"

$examples = [ordered]@{
    Motor              = @("T_J", "i_L", "T_B")
    Electrical         = @()
    Gyrator            = @()
    TransformerCascade = @()
    PumpAccumulator    = @()
    FloatingSource     = @()
    MultiNormalTree    = @()
}

function Normalize-AnalyzeOutput([string]$text) {
    if ($text.StartsWith([char]0xFEFF)) {
        $text = $text.Substring(1)
    }
    $text = $text -replace "`r`n", "`n" -replace "`r", "`n"
    if ($text.Length -eq 0) {
        return ""
    }
    return $text.TrimEnd("`n") + "`n"
}

$failed = @()
foreach ($name in $examples.Keys) {
    $path = Join-Path $root "Examples\$name.lgm"
    $goldenPath = Join-Path $goldenDir "$name.txt"
    if (-not (Test-Path $path)) {
        Write-Warning "Skip missing $path"
        continue
    }
    if (-not (Test-Path $goldenPath)) {
        $failed += "$name (missing golden $goldenPath)"
        continue
    }
    $args = @("--analyze", $path) + $examples[$name]
    $argStr = ($args | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '
    $actual = cmd /c "`"$lgm`" $argStr 2>nul"
    $expected = [System.IO.File]::ReadAllText($goldenPath)
    $actualNorm = Normalize-AnalyzeOutput(($actual -join "`n"))
    $expectedNorm = Normalize-AnalyzeOutput($expected)
    if ($actualNorm -ne $expectedNorm) {
        $failed += $name
        Write-Host "FAIL: $name"
    } else {
        Write-Host "OK: $name"
    }
}

if ($failed.Count -gt 0) {
    Write-Error "Golden check failed for: $($failed -join ', ')"
    exit 1
}

Write-Host "All $($examples.Count) golden benchmarks match."
