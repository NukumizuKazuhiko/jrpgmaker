#!/usr/bin/env pwsh
# Recompile all committed shader bytecode from the .hlsl sources (P1, ADR-003).
#
# The build consumes the committed bytecode in shaders/generated/ verbatim and
# never invokes a shader compiler. This script regenerates that bytecode from
# the .hlsl sources and is used by the CI shader-sync gate (git diff must be
# empty afterwards) and by developers after editing a shader.
#
# Requires a dxc binary. It is looked up, in order: $env:DXC, the vcpkg-managed
# copy under $VCPKG_ROOT/installed, or $PSScriptRoot sibling fallbacks.
#
# Usage:
#   pwsh ./tools/ci/compile_shaders.ps1 [-ShaderRoot <dir>] [-DxcPath <file>]

[CmdletBinding()]
param(
    [string]$ShaderRoot = (Join-Path $PSScriptRoot '..\..\shaders'),
    [string]$DxcPath = $env:DXC
)

$ErrorActionPreference = 'Stop'
$ShaderRoot = [IO.Path]::GetFullPath($ShaderRoot)
$GeneratedDir = Join-Path $ShaderRoot 'generated'

function Find-Dxc {
    if ($DxcPath -and (Test-Path $DxcPath)) { return $DxcPath }
    $candidates = @()
    if ($env:VCPKG_INSTALLED_DIR) {
        foreach ($triplet in @('x64-windows', 'x64-linux', 'x64-osx', 'arm64-osx')) {
            $candidates += Join-Path $env:VCPKG_INSTALLED_DIR ("$triplet\tools\directx-dxc\dxc.exe")
            $candidates += Join-Path $env:VCPKG_INSTALLED_DIR ("$triplet\tools\directx-dxc\dxc")
        }
    }
    if ($env:VCPKG_ROOT) {
        foreach ($triplet in @('x64-windows', 'x64-linux', 'x64-osx', 'arm64-osx')) {
            $candidates += Join-Path $env:VCPKG_ROOT ("installed\$triplet\tools\directx-dxc\dxc.exe")
            $candidates += Join-Path $env:VCPKG_ROOT ("installed\$triplet\tools\directx-dxc\dxc")
        }
    }
    if ($env:BUILD_DIR) {
        $candidates += Join-Path $env:BUILD_DIR "vcpkg_installed\x64-windows\tools\directx-dxc\dxc.exe"
        $candidates += Join-Path $env:BUILD_DIR "vcpkg_installed\x64-linux\tools\directx-dxc\dxc"
        $candidates += Join-Path $env:BUILD_DIR "vcpkg_installed\x64-windows\x64-windows\tools\directx-dxc\dxc.exe"
        $candidates += Join-Path $env:BUILD_DIR "vcpkg_installed\x64-linux\x64-linux\tools\directx-dxc\dxc"
    }
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    throw "dxc not found. Pass -DxcPath or set DXC/VCPKG_ROOT."
}

$dxc = Find-Dxc
Write-Host "Using dxc: $dxc"
& $dxc --version
if ($LASTEXITCODE -ne 0) { throw "dxc --version failed" }

foreach ($hlsl in Get-ChildItem -Path $ShaderRoot -Filter *.hlsl) {
    $base = [IO.Path]::GetFileNameWithoutExtension($hlsl.Name)
    $targets = @(
        @{ Profile = 'vs_6_0'; Entry = 'vs_main'; Suffix = 'vs'; Ext = 'dxil' },
        @{ Profile = 'ps_6_0'; Entry = 'ps_main'; Suffix = 'ps'; Ext = 'dxil' },
        @{ Profile = 'vs_6_0'; Entry = 'vs_main'; Suffix = 'vs'; Ext = 'spv'; Spirv = $true },
        @{ Profile = 'ps_6_0'; Entry = 'ps_main'; Suffix = 'ps'; Ext = 'spv'; Spirv = $true }
    )
    foreach ($target in $targets) {
        $out = Join-Path $GeneratedDir ("$base" + "_" + "$($target.Suffix)" + "." + "$($target.Ext)")
        $args = @()
        if ($target.Spirv) { $args += '-spirv' }
        $args += @('-T', $target.Profile, '-E', $target.Entry, '-Fo', $out, $hlsl.FullName)
        Write-Host "compiling $($hlsl.Name) -> $(Split-Path $out -Leaf)"
        & $dxc @args
        if ($LASTEXITCODE -ne 0) { throw "dxc failed for $($hlsl.Name) [$($target.Entry)]" }
    }
}

Write-Host "All shaders compiled into: $GeneratedDir"
