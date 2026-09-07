[CmdletBinding()]
param(
    [string]$RepoRoot = (Join-Path $PSScriptRoot '../..'),
    [string]$IndexPath = 'docs/README.md'
)

$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path -LiteralPath $RepoRoot).Path
$index = Join-Path $repo $IndexPath
if (-not (Test-Path -LiteralPath $index -PathType Leaf)) {
    throw "docs index does not exist: $IndexPath"
}

$indexText = Get-Content -Raw -LiteralPath $index
$indexRoot = Split-Path -Parent $index
$localLinks = [System.Collections.Generic.HashSet[string]]::new()
$currentTargets = [System.Collections.Generic.HashSet[string]]::new()
$linkMatches = [regex]::Matches($indexText, '\[[^\]]+\]\(([^)]+)\)')

foreach ($match in $linkMatches) {
    $target = $match.Groups[1].Value.Trim()
    if ($target -match '^(?:https?://|mailto:|#)') {
        continue
    }

    $pathPart = ($target -split '#', 2)[0]
    if ([string]::IsNullOrWhiteSpace($pathPart)) {
        $pathPart = $IndexPath
    }

    $candidate = [System.IO.Path]::GetFullPath((Join-Path $indexRoot $pathPart))
    $repoPrefix = $repo.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase) -and
        $candidate -ne $repo) {
        throw "docs index link escapes repository: $target"
    }
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "docs index link target does not exist: $target"
    }

    $relative = $candidate.Substring($repo.Length).TrimStart('\', '/').Replace('\', '/')
    if (-not $localLinks.Add($relative)) {
        continue
    }

    & git -C $repo ls-files --error-unmatch -- $relative *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "docs index link target is not tracked by Git: $relative"
    }
}

$lines = $indexText -split "`r?`n"
$inCurrentSection = $false
foreach ($line in $lines) {
    if ($line -match '^## 当前有效\s*$') {
        $inCurrentSection = $true
        continue
    }
    if ($inCurrentSection -and $line -match '^##\s+') {
        break
    }
    if (-not $inCurrentSection) {
        continue
    }

    $rowMatch = [regex]::Match($line, '^\|[^|]+\|\s*\[[^\]]+\]\(([^)]+)\)')
    if (-not $rowMatch.Success) {
        continue
    }
    $pathPart = ($rowMatch.Groups[1].Value -split '#', 2)[0]
    if ([string]::IsNullOrWhiteSpace($pathPart)) {
        $pathPart = $IndexPath
    }
    $candidate = [System.IO.Path]::GetFullPath((Join-Path $indexRoot $pathPart))
    $relative = $candidate.Substring($repo.Length).TrimStart('\', '/').Replace('\', '/')
    $currentTargets.Add($relative) | Out-Null
}

if ($currentTargets.Count -eq 0) {
    throw 'docs index has no current truth targets'
}

Write-Host ("check_docs_index: OK ({0} local links; {1} current targets tracked)" -f $localLinks.Count, $currentTargets.Count)
