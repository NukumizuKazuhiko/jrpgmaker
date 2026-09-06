[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$BuildRoot,
  [Parameter(Mandatory = $true)]
  [string]$OutputPath,
  [Parameter(Mandatory = $true)]
  [string]$EdgeClassOutputPath
)

$ErrorActionPreference = 'Stop'

$buildRootFull = [System.IO.Path]::GetFullPath($BuildRoot)
$outputPathFull = [System.IO.Path]::GetFullPath($OutputPath)
$edgeClassOutputPathFull = [System.IO.Path]::GetFullPath($EdgeClassOutputPath)
if ([StringComparer]::OrdinalIgnoreCase.Equals($outputPathFull, $edgeClassOutputPathFull)) {
  throw "runtime overlay golden outputs must be different files"
}
$testCandidates = @(
  (Join-Path $buildRootFull 'tests/unit/jrpgmaker_unit_tests.exe'),
  (Join-Path $buildRootFull 'tests/unit/jrpgmaker_unit_tests')
)
$testPath = $testCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($testPath)) {
  throw "runtime overlay golden generator cannot find jrpgmaker_unit_tests under '$buildRootFull'"
}

$outputDirectory = Split-Path -Parent $outputPathFull
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
  New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$edgeClassOutputDirectory = Split-Path -Parent $edgeClassOutputPathFull
if (-not [string]::IsNullOrWhiteSpace($edgeClassOutputDirectory)) {
  New-Item -ItemType Directory -Force -Path $edgeClassOutputDirectory | Out-Null
}

$oldGoldenWrite = $env:JRPGMAKER_GOLDEN_WRITE
$oldEdgeClassGoldenWrite = $env:JRPGMAKER_GOLDEN_EDGE_CLASS_WRITE
try {
  $env:JRPGMAKER_GOLDEN_WRITE = $outputPathFull
  $env:JRPGMAKER_GOLDEN_EDGE_CLASS_WRITE = $edgeClassOutputPathFull
  & $testPath '[rhi][golden][runtime][overlay][cjk]' --reporter console
  $testExitCode = if ($null -eq $LASTEXITCODE) { 1 } else { [int]$LASTEXITCODE }
  if ($testExitCode -ne 0) {
    throw "runtime overlay CJK golden generation test failed with exit code $testExitCode"
  }
}
finally {
  if ($null -eq $oldGoldenWrite) {
    Remove-Item Env:JRPGMAKER_GOLDEN_WRITE -ErrorAction SilentlyContinue
  }
  else {
    $env:JRPGMAKER_GOLDEN_WRITE = $oldGoldenWrite
  }
  if ($null -eq $oldEdgeClassGoldenWrite) {
    Remove-Item Env:JRPGMAKER_GOLDEN_EDGE_CLASS_WRITE -ErrorAction SilentlyContinue
  }
  else {
    $env:JRPGMAKER_GOLDEN_EDGE_CLASS_WRITE = $oldEdgeClassGoldenWrite
  }
}

if (-not (Test-Path -LiteralPath $outputPathFull -PathType Leaf)) {
  throw "runtime overlay CJK golden generator did not produce '$outputPathFull'"
}
if (-not (Test-Path -LiteralPath $edgeClassOutputPathFull -PathType Leaf)) {
  throw "runtime overlay CJK edge-class generator did not produce '$edgeClassOutputPathFull'"
}

Write-Output "generated runtime overlay CJK golden: $outputPathFull"
Write-Output "generated runtime overlay CJK edge-class golden: $edgeClassOutputPathFull"
