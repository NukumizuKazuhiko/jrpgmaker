[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..' '..')).Path
$engineRoot = (Resolve-Path (Join-Path $repoRoot 'engine')).Path

$includePattern = '^(?:#include\s*)[<"](.*?)[">]'

function Get-SourceFiles {
  param([string]$Base)
  Get-ChildItem -LiteralPath $Base -Recurse -File -Include *.cpp, *.hpp |
    Where-Object { $_.FullName -notmatch '[\\/](build|vcpkg_installed)[\\/]' }
}

function Get-PrivateHeaderOwner {
  param([string]$IncludingFile, [string]$IncludeText)

  if ($IncludeText -match '(^|[\\/])src([\\/]|$)') {
    if ($IncludeText -notmatch '\.\.') { return '<suspicious-src-path>' }
  }

  if ($IncludeText -notmatch '\.\.') { return $null }

  $candidate = Join-Path (Split-Path -Parent $IncludingFile) $IncludeText
  try {
    $resolved = [System.IO.Path]::GetFullPath($candidate)
  }
  catch {
    return $null
  }

  if (-not $resolved.StartsWith($engineRoot, [System.StringComparison]::OrdinalIgnoreCase)) { return $null }

  $relative = $resolved.Substring($engineRoot.Length).TrimStart('\', '/')
  if ($relative -match '^([^\\/]+)[\\/]src([\\/]|$)') { return $Matches[1] }

  return $null
}

function Get-ConsumerModule {
  param([string]$FileFullName)

  $relative = $FileFullName.Substring($repoRoot.Length).TrimStart('\', '/')
  if ($relative -match '^engine[\\/]([^\\/]+)[\\/]') { return $Matches[1] }
  return '<outside-engine>'
}

$failures = @()
$files = @(Get-SourceFiles -Base $repoRoot)

foreach ($file in $files) {
  $lineNumber = 0
  foreach ($line in Get-Content -LiteralPath $file.FullName) {
    $lineNumber++
    if ($line -notmatch $includePattern) { continue }
    $owner = Get-PrivateHeaderOwner -IncludingFile $file.FullName -IncludeText $Matches[1]
    if (-not $owner) { continue }

    $consumer = Get-ConsumerModule -FileFullName $file.FullName
    $isOwnModule = ($owner -eq $consumer)

    if (-not $isOwnModule) {
      $failures += "private header leak: $($file.FullName):${lineNumber} -> owner '$owner', consumer '$consumer'"
    }
  }
}

if ($failures.Count -gt 0) {
  $failures | ForEach-Object { Write-Error $_ }
  exit 1
}

Write-Host "check_private_headers: OK ($($files.Count) files scanned)"
exit 0
