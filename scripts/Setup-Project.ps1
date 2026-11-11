# Project setup using the vcpkg bundled with Visual Studio 2026.
# No git clone, no vcpkg bootstrap, no global PATH changes.
param(
    [string]$VcpkgRoot = '',
    [switch]$Build
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Invoke-Checked([string]$exe, [string[]]$arguments) {
    Write-Host ('> ' + $exe + ' ' + ($arguments -join ' '))
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("Command failed with exit code {0}: {1}" -f $LASTEXITCODE, $exe)
    }
}

$pf86 = ${env:ProgramFiles(x86)}
if (-not $pf86) { $pf86 = 'C:\Program Files (x86)' }
$vswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe missing. Inspect the Visual Studio 2026 installation.' }
$vsPath = ((& $vswhere -latest -products '*' -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) -join '').Trim()
if (-not $vsPath) { throw 'Visual Studio 2026 with MSVC tools was not found.' }

$toolDir = Join-Path $vsPath 'VC\Tools\MSVC'
$msvc = @(Get-ChildItem $toolDir -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '14.51.*' })
if ($msvc.Count -eq 0) { throw 'MSVC 14.51 is missing. Install it side-by-side using Visual Studio Installer.' }
$sdk = Join-Path $pf86 'Windows Kits\10\Include\10.0.26100.0'
if (-not (Test-Path $sdk)) { throw 'Windows SDK 10.0.26100.0 is missing.' }
$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { throw 'Visual Studio 2026 CMake is missing.' }

if (-not $VcpkgRoot) { $VcpkgRoot = Join-Path $vsPath 'VC\vcpkg' }
$VcpkgRoot = [System.IO.Path]::GetFullPath($VcpkgRoot)
$vcpkgExe = Join-Path $VcpkgRoot 'vcpkg.exe'
$vcpkgCmake = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path $vcpkgExe)) { throw ('vcpkg.exe not found in: ' + $VcpkgRoot) }
if (-not (Test-Path $vcpkgCmake)) {
    throw ('vcpkg CMake integration file not found: ' + $vcpkgCmake + '. The VS-bundled layout needs inspection before proceeding.')
}

Write-Host ('VS: ' + $vsPath)
Write-Host ('MSVC: ' + ($msvc.Name -join ', '))
Write-Host 'SDK: 10.0.26100.0'
Write-Host ('CMake: ' + ((& $cmake --version | Select-Object -First 1) -join ''))
Write-Host ('vcpkg: ' + $vcpkgExe)
Invoke-Checked $vcpkgExe @('version')
Write-Host 'Dependencies: manifest with pinned registry baseline (vcpkg.json)'

# Keep this machine's absolute paths out of the shared Git repository.
$userPresets = Join-Path $repo 'CMakeUserPresets.json'
if (-not (Test-Path $userPresets)) {
    $obj = [ordered]@{
        version = 6
        configurePresets = @([ordered]@{
            name = 'windows-vs2026-x64'
            inherits = 'windows-vs2026-base'
            environment = [ordered]@{ VCPKG_ROOT = $VcpkgRoot.Replace('\', '/') }
        })
        buildPresets = @(
            [ordered]@{ name = 'debug'; configurePreset = 'windows-vs2026-x64'; configuration = 'Debug' },
            [ordered]@{ name = 'release'; configurePreset = 'windows-vs2026-x64'; configuration = 'Release' }
        )
    }
    $json = $obj | ConvertTo-Json -Depth 9
    [System.IO.File]::WriteAllText($userPresets, $json + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ('Created local-only: ' + $userPresets)
} else {
    $presets = Get-Content $userPresets -Raw | ConvertFrom-Json
    $existingRoot = @($presets.configurePresets | Where-Object { $_.name -eq 'windows-vs2026-x64' } | ForEach-Object { $_.environment.VCPKG_ROOT })
    if ($existingRoot.Count -ne 1 -or $existingRoot[0] -ne $VcpkgRoot.Replace('\', '/')) {
        throw 'Existing CMakeUserPresets.json has different machine-local settings. No overwrite performed; please inspect it.'
    }
    Write-Host 'Reusing CMakeUserPresets.json.'
}

if ($Build) {
    Push-Location $repo
    try {
        Invoke-Checked $cmake @('--preset', 'windows-vs2026-x64')
        Invoke-Checked $cmake @('--build', '--preset', 'debug')
        $exe = Join-Path $repo 'out\build\windows-vs2026-x64\Debug\armor_demo.exe'
        if (-not (Test-Path $exe)) { throw ('Expected executable not found: ' + $exe) }
        Invoke-Checked $exe @('--no-gui')
        Write-Host 'Project configure/build/demo finished successfully on this machine.'
    } finally { Pop-Location }
} else {
    Write-Host 'Add -Build to configure, download/build OpenCV dependencies and run the demo.'
}
