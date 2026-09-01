[CmdletBinding()]
param(
    [string]$PackageScript = (Join-Path $PSScriptRoot 'package_release.ps1')
)

$ErrorActionPreference = 'Stop'

$tmpRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("jrpgmaker_release_selftest_" + [System.Guid]::NewGuid().ToString('N'))

function Write-JsonFile([string]$Path, [object]$Value) {
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    [System.IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 8) + "`n",
        [System.Text.UTF8Encoding]::new($false))
}

function New-Fixture([object[]]$Plugins) {
    $root = Join-Path $tmpRoot ([System.Guid]::NewGuid().ToString('N'))
    $build = Join-Path $root 'build'
    $project = Join-Path $root 'project'
    New-Item -ItemType Directory -Force -Path (Join-Path $build 'app') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $project 'assets/data') | Out-Null
    Set-Content -LiteralPath (Join-Path $build 'app/jrpgmaker_app.exe') -Value 'app'
    Set-Content -LiteralPath (Join-Path $project 'assets/data/project.json') -Value '{}'
    Write-JsonFile (Join-Path $project 'project.json') @{ schema = 1; id = 'selftest' }
    foreach ($plugin in $Plugins) {
        $pluginRoot = Join-Path (Join-Path $project 'plugins') $plugin.path
        New-Item -ItemType Directory -Force -Path $pluginRoot | Out-Null
        Write-JsonFile (Join-Path $pluginRoot 'plugin.json') @{
            schema = 1
            id = $plugin.id
            type = 'render_style'
            version = 1
            engine_contract = 1
            data_roots = @($plugin.roots)
            capabilities = @()
        }
        foreach ($file in $plugin.files.GetEnumerator()) {
            $filePath = Join-Path $pluginRoot $file.Key
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $filePath) | Out-Null
            Set-Content -LiteralPath $filePath -Value $file.Value
        }
    }
    return [pscustomobject]@{ Build = $build; Project = $project; Root = $root }
}

function Invoke-Package([object]$Fixture, [string]$OutputName) {
    $output = Join-Path $Fixture.Root $OutputName
    $lines = @(& pwsh -NoProfile -File $PackageScript -BuildRoot $Fixture.Build -ProjectRoot $Fixture.Project -OutputRoot $output 2>&1)
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $lines; Path = $output }
}

function Assert-Throws([object]$Fixture, [string]$Name, [string]$Pattern) {
    $result = Invoke-Package $Fixture $Name
    if ($result.ExitCode -eq 0) {
        throw "selftest FAILED: $Name unexpectedly succeeded"
    }
    $text = $result.Output | Out-String
    if ($text -notmatch $Pattern) {
        throw "selftest FAILED: $Name did not report '$Pattern'.`n$text"
    }
}

try {
    New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null

    $valid = New-Fixture @(
        [pscustomobject]@{
            path = 'vendor/custom'; id = 'custom'; roots = @('plugins/vendor/custom/content', 'plugins/vendor/custom/tables/encounters')
            files = [ordered]@{ 'content/readme.txt' = 'custom'; 'tables/encounters/demo.json' = '{"id":"demo"}' }
        }
    )
    $first = Invoke-Package $valid 'out-one'
    if ($first.ExitCode -ne 0) { throw "selftest FAILED: valid custom roots failed: $($first.Output | Out-String)" }
    foreach ($path in @('plugins/vendor/custom/content/readme.txt', 'plugins/vendor/custom/tables/encounters/demo.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $first.Path $path) -PathType Leaf)) {
            throw "selftest FAILED: expected packaged root file is missing: $path"
        }
    }
    if (Test-Path -LiteralPath (Join-Path $first.Path 'plugins/vendor/custom/data')) {
        throw 'selftest FAILED: undeclared data directory was packaged'
    }

    $second = Invoke-Package $valid 'out-two'
    if ($second.ExitCode -ne 0) { throw "selftest FAILED: deterministic second package failed: $($second.Output | Out-String)" }
    $hashOne = (Get-FileHash -LiteralPath (Join-Path $first.Path 'release-manifest.json')).Hash
    $hashTwo = (Get-FileHash -LiteralPath (Join-Path $second.Path 'release-manifest.json')).Hash
    if ($hashOne -ne $hashTwo) { throw 'selftest FAILED: release manifest is not deterministic' }

    $missing = New-Fixture @([pscustomobject]@{
            path = 'missing'; id = 'missing'; roots = @('plugins/missing/content'); files = [ordered]@{}
        })
    Assert-Throws $missing 'missing-output' 'data root does not exist'

    $escape = New-Fixture @([pscustomobject]@{
            path = 'escape'; id = 'escape'; roots = @('../outside'); files = [ordered]@{}
        })
    Assert-Throws $escape 'escape-output' 'invalid plugin manifest|invalid plugin data root'

    $packageRoot = New-Fixture @([pscustomobject]@{
            path = 'package-root'; id = 'package.root'; roots = @('plugins'); files = [ordered]@{}
        })
    Assert-Throws $packageRoot 'package-root-output' 'cannot preserve package-relative path'

    $duplicate = New-Fixture @([pscustomobject]@{
            path = 'duplicate'; id = 'duplicate'; roots = @('plugins/duplicate/content', 'plugins/duplicate/content');
            files = [ordered]@{ 'content/data.txt' = 'duplicate' }
        })
    Assert-Throws $duplicate 'duplicate-output' 'duplicate plugin data root|conflicting plugin data root'

    $overlap = New-Fixture @([pscustomobject]@{
            path = 'overlap'; id = 'overlap'; roots = @('plugins/overlap/content', 'plugins/overlap/content/nested');
            files = [ordered]@{ 'content/root.txt' = 'root'; 'content/nested/child.txt' = 'child' }
        })
    Assert-Throws $overlap 'overlap-output' 'conflicting plugin data root'

    $destinationConflict = New-Fixture @(
        [pscustomobject]@{ path = 'vendor-a/shared'; id = 'vendor.a'; roots = @('plugins/vendor-a/shared/content'); files = [ordered]@{ 'content/a.txt' = 'a' } }
        [pscustomobject]@{ path = 'vendor-b/shared'; id = 'vendor.b'; roots = @('plugins/vendor-b/shared/content'); files = [ordered]@{ 'content/b.txt' = 'b' } }
    )
    Assert-Throws $destinationConflict 'destination-output' 'conflicting plugin output directory'

    Write-Host 'selftest_package_release: OK'
}
finally {
    Remove-Item -LiteralPath $tmpRoot -Recurse -Force -ErrorAction SilentlyContinue
}
