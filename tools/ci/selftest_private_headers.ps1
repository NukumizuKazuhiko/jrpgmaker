[CmdletBinding()]
param(
  [string]$CheckScript = (Join-Path $PSScriptRoot 'check_private_headers.ps1')
)

$ErrorActionPreference = 'Stop'

$tmpRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("jrpgmaker_ph_selftest_" + [System.Guid]::NewGuid().ToString('N'))
$engineRoot = Join-Path $tmpRoot 'engine'
New-Item -ItemType Directory -Force -Path (Join-Path $engineRoot 'alpha\src') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $engineRoot 'beta\src') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $engineRoot 'gamma\include') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $tmpRoot 'tools') | Out-Null

Set-Content -LiteralPath (Join-Path $engineRoot 'alpha\src\alpha_private.hpp') -Value '// private header of alpha'
Set-Content -LiteralPath (Join-Path $engineRoot 'alpha\src\alpha.cpp') -Value @'
#include "alpha_private.hpp"
// same-module private include: allowed
'@
Set-Content -LiteralPath (Join-Path $engineRoot 'beta\src\beta.cpp') -Value @'
#include "../../alpha/src/alpha_private.hpp"
// cross-module private include: must be rejected
'@
Set-Content -LiteralPath (Join-Path $engineRoot 'beta\src\beta2.cpp') -Value @'
#include <alpha/src/alpha_private.hpp>
// suspicious angle-bracket path containing /src/: must be rejected
'@
Set-Content -LiteralPath (Join-Path $engineRoot 'gamma\include\gamma.hpp') -Value '// public header of gamma'
Set-Content -LiteralPath (Join-Path $engineRoot 'beta\src\beta3.cpp') -Value @'
#include "../gamma/include/gamma.hpp"
// public header outside any /src/: allowed
'@
Set-Content -LiteralPath (Join-Path $tmpRoot 'tools\tool.cpp') -Value @'
#include "../engine/alpha/src/alpha_private.hpp"
// consumer outside engine: must be rejected
'@

$scriptFailed = $false
try {
  $output = & pwsh -NoProfile -File $CheckScript -RepoRoot $tmpRoot 2>&1
  $exitCode = $LASTEXITCODE
  $outputText = ($output | Out-String)

  $expectedExit = 1
  if ($exitCode -ne $expectedExit) {
    Write-Error "selftest FAILED: expected exit code 1 (leak found), got $exitCode"
    $scriptFailed = $true
  }
  else {
    Write-Host "exit code 1 as expected"
  }

  $crossModule = ($outputText -match 'beta[\\/]src[\\/]beta\.cpp:1 -> owner ''alpha'', consumer ''beta''')
  $suspicious = ($outputText -match 'beta[\\/]src[\\/]beta2\.cpp:1 -> owner ''<suspicious-src-path>''')
  $outside = ($outputText -match 'tool\.cpp:1 -> owner ''alpha'', consumer ''<outside-engine>''')
  $noAlphaLeak = ($outputText -notmatch 'alpha[\\/]src[\\/]alpha\.cpp')
  $noPublicLeak = ($outputText -notmatch 'beta3\.cpp')

  if (-not $crossModule) { Write-Error 'selftest FAILED: cross-module leak not reported'; $scriptFailed = $true }
  if (-not $suspicious) { Write-Error 'selftest FAILED: suspicious src-path include not reported'; $scriptFailed = $true }
  if (-not $outside) { Write-Error 'selftest FAILED: outside-engine consumer not reported'; $scriptFailed = $true }
  if (-not $noAlphaLeak) { Write-Error 'selftest FAILED: same-module private include falsely reported'; $scriptFailed = $true }
  if (-not $noPublicLeak) { Write-Error 'selftest FAILED: public header include falsely reported'; $scriptFailed = $true }

  if ($scriptFailed) {
    Write-Host '--- actual output ---'
    Write-Host $outputText
    throw 'private-headers selftest failed'
  }

  Write-Host "selftest_private_headers: OK ($($outputText.Trim().Split("`n").Count) diagnostic lines)"
}
finally {
  Remove-Item -LiteralPath $tmpRoot -Recurse -Force -ErrorAction SilentlyContinue
}