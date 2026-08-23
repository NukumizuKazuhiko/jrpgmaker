[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..' '..')).Path

$srcHeaderPattern = '^(?:#include\s*)[<"](.*?)[">]'

function Get-SourceFiles {
  param([string]$Base)
  Get-ChildItem -LiteralPath $Base -Recurse -File -Include *.cpp, *.hpp |
    Where-Object { $_.FullName -notmatch '[\\/]build[\\/]' }
}

function Resolve-IncludeTarget {
  param([string]$IncludingFile, [string]$IncludeText)

  if ($IncludeText -match '(^|[\\/])src([\\/]|$)') { return 'pattern-src' }

  if ($IncludeText -notmatch '\.\.') { return $null }

  $candidate = Join-Path (Split-Path -Parent $IncludingFile) $IncludeText
  $resolved = [System.IO.Path]::GetFullPath($candidate)
  return $resolved
}

$failures = @()
$files = @(Get-SourceFiles -Base $repoRoot)

foreach ($file in $files) {
  $lines = Get-Content -LiteralPath $file.FullName
  for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -notmatch $srcHeaderPattern) { continue }
    $target = Resolve-IncludeTarget -IncludingFile $file.FullName -IncludeText $Matches[1]
    if (-not $target) { continue }

    $violating = $false
    if ($target -eq 'pattern-src') {
      $violating = $true
      $ownerModule = '<unresolvable>'
    }
    else {
      if ($target -match [regex]::Escape("$repoRoot\engine\") -or $target -match "$repoRoot/engine/") {
        $rel = $target.Substring($repoRoot.Length + 1)
        if ($rel -match '^engine[\\/]([^\\/]+)[\\/]src[\\/]') {
          $ownerModule = $Matches[1]
          $fileRel = $file.FullName.Substring($repoRoot.Length + 1)
          if ($fileRel -notmatch "^engine[\\/]$([regex]::Escape($ownerModule))[\\/]") {
            $violating = $true
          }
        }
      }
    }

    if ($violating) {
      $failures += "private header leak: $($file.FullName):$($i + 1) -> module '$ownerModule'"
    }
  }
}

if ($failures.Count -gt 0) {
  $failures | ForEach-Object { Write-Error $_ }
  exit 1
}

Write-Host "check_private_headers: OK ($($files.Count) files scanned)"
exit 0
