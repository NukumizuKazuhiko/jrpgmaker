[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$BuildRoot,
    [Parameter(Mandatory = $true)] [string]$ProjectRoot,
    [Parameter(Mandatory = $true)] [string]$OutputRoot,
    [string]$ExecutableName
)

$ErrorActionPreference = 'Stop'
$maxFiles = 4096
$maxBytes = 512MB
$currentContract = 1

function Resolve-ExistingDirectory([string]$Path, [string]$Name) {
    $resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Container)) {
        throw "$Name does not exist: $resolved"
    }
    return $resolved
}

function Test-PathWithin([string]$Root, [string]$Candidate) {
    $relative = [System.IO.Path]::GetRelativePath($Root, $Candidate)
    return -not [string]::IsNullOrEmpty($relative) -and
        -not [System.IO.Path]::IsPathRooted($relative) -and
        $relative -ne '..' -and
        -not $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -and
        -not $relative.StartsWith('../') -and
        -not $relative.StartsWith('..\')
}

function Resolve-CanonicalPath([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    $target = $item.ResolveLinkTarget($true)
    if ($null -ne $target) {
        return [System.IO.Path]::GetFullPath($target.FullName)
    }
    return [System.IO.Path]::GetFullPath($item.FullName)
}

function Get-NormalizedManifestRoot([string]$Root) {
    if ([string]::IsNullOrWhiteSpace($Root) -or
        $Root -match '(^|[\\/])\.\.([\\/]|$)' -or
        [System.IO.Path]::IsPathRooted($Root) -or
        $Root -match '^[A-Za-z]:') {
        throw "invalid plugin data root: $Root"
    }
    $normalized = $Root.Replace('\', '/')
    if ($normalized.StartsWith('/') -or $normalized.Contains('//')) {
        throw "invalid plugin data root: $Root"
    }
    try {
        $segments = @($normalized.Split('/') | Where-Object { $_ -ne '.' })
        if ($segments.Count -eq 0) {
            throw "invalid plugin data root: $Root"
        }
        return ($segments -join '/').TrimEnd('/')
    } catch {
        throw "invalid plugin data root: $Root"
    }
}

$pathComparer = if ($IsWindows) {
    [System.StringComparer]::OrdinalIgnoreCase
} else {
    [System.StringComparer]::Ordinal
}

$build = Resolve-ExistingDirectory $BuildRoot 'BuildRoot'
$project = Resolve-ExistingDirectory $ProjectRoot 'ProjectRoot'
$canonicalProject = Resolve-CanonicalPath $project
$output = [System.IO.Path]::GetFullPath($OutputRoot)
$projectManifest = Join-Path $project 'project.json'
if (-not (Test-Path -LiteralPath $projectManifest -PathType Leaf)) {
    $demoManifest = Join-Path $project 'assets/data/project_demo.json'
    if (Test-Path -LiteralPath $demoManifest -PathType Leaf) {
        $projectManifest = $demoManifest
    } else {
        throw "project manifest is missing: $projectManifest"
    }
}
if (Test-Path -LiteralPath $output) {
    throw "OutputRoot already exists: $output"
}
New-Item -ItemType Directory -Path $output | Out-Null

$resolvedExecutableName = $ExecutableName
if ([string]::IsNullOrWhiteSpace($resolvedExecutableName)) {
    $resolvedExecutableName = if ($IsWindows) { 'jrpgmaker_app.exe' } else { 'jrpgmaker_app' }
}
$app = Join-Path (Join-Path $build 'app') $resolvedExecutableName
if (-not (Test-Path -LiteralPath $app -PathType Leaf)) {
    throw "built application is missing: $app"
}
New-Item -ItemType Directory -Path (Join-Path $output 'bin') | Out-Null
$runtimeFiles = @(Get-ChildItem -LiteralPath (Join-Path $build 'app') -File |
    Where-Object {
        $_.Name -eq $resolvedExecutableName -or
        @('.dll', '.so', '.dylib') -contains $_.Extension.ToLowerInvariant()
    })
if ($runtimeFiles.Count -eq 0) {
    throw "no application runtime files found under: $(Join-Path $build 'app')"
}
Copy-Item -LiteralPath $runtimeFiles.FullName -Destination (Join-Path $output 'bin')
Copy-Item -LiteralPath $projectManifest -Destination (Join-Path $output 'project.json')
Copy-Item -LiteralPath (Join-Path $project 'assets') -Destination $output -Recurse

$pluginOutput = Join-Path $output 'plugins'
New-Item -ItemType Directory -Path $pluginOutput | Out-Null
$pluginContracts = @()
$pluginIds = [System.Collections.Generic.HashSet[string]]::new($pathComparer)
$pluginOutputDirectories = [System.Collections.Generic.HashSet[string]]::new($pathComparer)
$packagedDataRoots = @()
$packagedDataFiles = [System.Collections.Generic.HashSet[string]]::new($pathComparer)
$pluginManifests = @(Get-ChildItem -LiteralPath (Join-Path $project 'plugins') -Filter plugin.json -File -Recurse |
    Sort-Object FullName)
if ($pluginManifests.Count -eq 0) {
    throw 'no plugin manifests found'
}
foreach ($manifest in $pluginManifests) {
    $manifestDocument = Get-Content -LiteralPath $manifest.FullName -Raw | ConvertFrom-Json
    $validTypes = @('battle', 'render_style')
    $hasRequiredArrays = $null -ne $manifestDocument.data_roots -and
        $null -ne $manifestDocument.capabilities
    $validArrays = $hasRequiredArrays -and
        $manifestDocument.data_roots -is [array] -and
        $manifestDocument.capabilities -is [array]
    $validRoots = $validArrays -and
        (@($manifestDocument.data_roots) | Where-Object {
            [string]::IsNullOrWhiteSpace([string]$_) -or
            [string]$_ -match '(^|[\\/])\.\.([\\/]|$)' -or [System.IO.Path]::IsPathRooted([string]$_)
        }).Count -eq 0
    $validCapabilities = $validArrays -and
        (@($manifestDocument.capabilities) | Where-Object {
            [string]::IsNullOrWhiteSpace([string]$_)
        }).Count -eq 0 -and
        (@($manifestDocument.capabilities | ForEach-Object { [string]$_ } | Sort-Object -Unique).Count -eq @($manifestDocument.capabilities).Count)
    if ($manifestDocument.schema -ne 1 -or
        [string]::IsNullOrWhiteSpace([string]$manifestDocument.id) -or
        $validTypes -notcontains [string]$manifestDocument.type -or
        [int]$manifestDocument.version -le 0 -or
        [int]$manifestDocument.engine_contract -ne $currentContract -or
        -not $validRoots -or -not $validCapabilities) {
        throw "invalid plugin manifest: $($manifest.FullName)"
    }
    if (-not $pluginIds.Add([string]$manifestDocument.id)) {
        throw "duplicate plugin id: $($manifestDocument.id)"
    }
    $pluginContracts += [int]$manifestDocument.engine_contract
    $destinationName = Split-Path -Leaf $manifest.Directory.FullName
    if (-not $pluginOutputDirectories.Add($destinationName)) {
        throw "conflicting plugin output directory: $destinationName"
    }
    $destination = Join-Path $pluginOutput $destinationName

    $rootRecords = @()
    foreach ($rootValue in @($manifestDocument.data_roots)) {
        $root = Get-NormalizedManifestRoot ([string]$rootValue)
        $sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $project $root))
        if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
            throw "plugin data root does not exist: $($manifest.FullName): $root"
        }
        $canonicalRoot = Resolve-CanonicalPath $sourceRoot
        if (-not (Test-PathWithin $canonicalProject $canonicalRoot) -or
            $canonicalRoot -eq $canonicalProject) {
            throw "plugin data root escapes project directory: $($manifest.FullName): $root"
        }
        $destinationRoot = [System.IO.Path]::GetFullPath((Join-Path $output $root))
        $relativeDestination = [System.IO.Path]::GetRelativePath($output, $destinationRoot)
        if ([System.IO.Path]::IsPathRooted($relativeDestination) -or
            $relativeDestination -eq '..' -or
            $relativeDestination.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or
            $relativeDestination -eq '.' -or
            $destinationRoot -eq [System.IO.Path]::GetFullPath($pluginOutput)) {
            throw "plugin data root cannot preserve package-relative path: $($manifest.FullName): $root"
        }
        foreach ($previous in $packagedDataRoots + $rootRecords) {
            if ((Test-PathWithin $previous.DestinationRoot $destinationRoot) -or
                (Test-PathWithin $destinationRoot $previous.DestinationRoot)) {
                throw "conflicting plugin data root: $($manifest.FullName): $root"
            }
        }
        $sourceFiles = @(Get-ChildItem -LiteralPath $sourceRoot -File -Recurse -Force |
            Sort-Object FullName)
        foreach ($sourceFile in $sourceFiles) {
            $canonicalFile = Resolve-CanonicalPath $sourceFile.FullName
            if (-not (Test-PathWithin $canonicalRoot $canonicalFile)) {
                throw "plugin data file escapes declared root: $($manifest.FullName): $($sourceFile.FullName)"
            }
            if ([System.IO.Path]::GetFullPath($manifest.FullName) -eq $canonicalFile) {
                throw "plugin data root includes its manifest: $($manifest.FullName): $root"
            }
        }
        $rootRecords += [pscustomobject]@{
            Name = $root
            SourceRoot = $sourceRoot
            CanonicalRoot = $canonicalRoot
            DestinationRoot = $destinationRoot
            SourceFiles = $sourceFiles
        }
    }
    New-Item -ItemType Directory -Path $destination | Out-Null
    Copy-Item -LiteralPath $manifest.FullName -Destination $destination
    foreach ($rootRecord in $rootRecords) {
        New-Item -ItemType Directory -Force -Path $rootRecord.DestinationRoot | Out-Null
        foreach ($sourceFile in $rootRecord.SourceFiles) {
            $relativeFile = [System.IO.Path]::GetRelativePath($rootRecord.SourceRoot, $sourceFile.FullName)
            $targetFile = Join-Path $rootRecord.DestinationRoot $relativeFile
            $targetFile = [System.IO.Path]::GetFullPath($targetFile)
            if ((Test-Path -LiteralPath $targetFile) -or -not $packagedDataFiles.Add($targetFile)) {
                throw "conflicting plugin data target: $targetFile"
            }
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $targetFile) | Out-Null
            Copy-Item -LiteralPath $sourceFile.FullName -Destination $targetFile
        }
        $packagedDataRoots += $rootRecord
    }
}
$contracts = @($pluginContracts | Sort-Object -Unique)
if ($contracts.Count -ne 1 -or $contracts[0] -ne $currentContract) {
    throw "plugin manifests declare incompatible engine contracts"
}

$files = @(Get-ChildItem -LiteralPath $output -File -Recurse | Sort-Object FullName)
if ($files.Count -gt $maxFiles) {
    throw "release file budget exceeded: $($files.Count) > $maxFiles"
}
$totalBytes = [int64]0
$entries = foreach ($file in $files) {
    $relative = [System.IO.Path]::GetRelativePath($output, $file.FullName).Replace('\', '/')
    $totalBytes += $file.Length
    [pscustomobject][ordered]@{
        path = $relative
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
if ($totalBytes -gt $maxBytes) {
    throw "release byte budget exceeded: $totalBytes > $maxBytes"
}
$releaseManifest = [pscustomobject][ordered]@{
    schema = 1
    engine_contract = $contracts[0]
    files = @($entries)
}
$json = $releaseManifest | ConvertTo-Json -Depth 4 -Compress
[System.IO.File]::WriteAllText((Join-Path $output 'release-manifest.json'), $json + "`n",
    [System.Text.UTF8Encoding]::new($false))
Write-Output "release package created: $output ($($files.Count) files, $totalBytes bytes)"
