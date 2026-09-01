[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$BuildRoot,
  [Parameter(Mandatory = $true)]
  [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

$buildRootFull = [System.IO.Path]::GetFullPath($BuildRoot)
$outputPathFull = [System.IO.Path]::GetFullPath($OutputPath)
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

$oldGoldenWrite = $env:JRPGMAKER_GOLDEN_WRITE
try {
  $env:JRPGMAKER_GOLDEN_WRITE = $outputPathFull
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
}

if (-not (Test-Path -LiteralPath $outputPathFull -PathType Leaf)) {
  throw "runtime overlay CJK golden generator did not produce '$outputPathFull'"
}

Write-Output "generated runtime overlay CJK golden: $outputPathFull"
