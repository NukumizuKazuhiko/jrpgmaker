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

$build = Resolve-ExistingDirectory $BuildRoot 'BuildRoot'
$project = Resolve-ExistingDirectory $ProjectRoot 'ProjectRoot'
$output = [System.IO.Path]::GetFullPath($OutputRoot)
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
Copy-Item -LiteralPath (Join-Path $project 'assets') -Destination $output -Recurse

$pluginOutput = Join-Path $output 'plugins'
New-Item -ItemType Directory -Path $pluginOutput | Out-Null
$pluginContracts = @()
$pluginIds = [System.Collections.Generic.HashSet[string]]::new()
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
    $destination = Join-Path $pluginOutput (Split-Path -Leaf $manifest.Directory.FullName)
    New-Item -ItemType Directory -Path $destination | Out-Null
    Copy-Item -LiteralPath $manifest.FullName -Destination $destination
    $data = Join-Path $manifest.Directory.FullName 'data'
    if (Test-Path -LiteralPath $data -PathType Container) {
        Copy-Item -LiteralPath $data -Destination $destination -Recurse
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
