# Read-only diagnostic for a desktop or laptop. Compatible with Windows PowerShell 5.1.
$ErrorActionPreference = 'Continue'
Write-Host '=== Windows / PowerShell ==='
Write-Host ("OS: " + [Environment]::OSVersion.VersionString)
Write-Host ("64-bit OS: " + [Environment]::Is64BitOperatingSystem)
Write-Host ("64-bit process: " + [Environment]::Is64BitProcess)
Write-Host ("PowerShell: " + $PSVersionTable.PSVersion.ToString())

Write-Host "`n=== Commands in current PATH (paths only) ==="
foreach ($name in @('git.exe','cmake.exe','cl.exe','link.exe','msbuild.exe','ninja.exe','vcpkg.exe','code.cmd','gh.exe')) {
    $found = @(Get-Command $name -All -ErrorAction SilentlyContinue | Select-Object -First 3)
    if ($found.Count -eq 0) {
        Write-Host ("{0}: not found in PATH" -f $name)
    } else {
        foreach ($item in $found) {
            Write-Host ("{0}: {1}" -f $name, $item.Source)
        }
    }
}

$pf86 = ${env:ProgramFiles(x86)}
if (-not $pf86) { $pf86 = 'C:\Program Files (x86)' }
$vswhere = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
Write-Host "`n=== Visual Studio instances ==="
if (Test-Path $vswhere) {
    $json = (& $vswhere -all -products '*' -format json) -join "`n"
    $instances = @($json | ConvertFrom-Json)
    foreach ($instance in $instances) {
        if ($null -eq $instance -or -not $instance.installationPath) { continue }
        $root = [string]$instance.installationPath
        Write-Host ("VS: {0} | {1} | complete={2}" -f $instance.installationVersion, $root, $instance.isComplete)
        $toolRoot = Join-Path $root 'VC\Tools\MSVC'
        if (Test-Path $toolRoot) {
            $dirs = @(Get-ChildItem $toolRoot -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
            Write-Host ("  MSVC installed folders: " + ($dirs -join ', '))
        } else {
            Write-Host '  No MSVC tools directory found'
        }
        $cm = Join-Path $root 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path $cm) {
            Write-Host ("  VS CMake: " + ((& $cm --version | Select-Object -First 1) -join ''))
        } else { Write-Host '  VS CMake: not found' }
        $vp = Join-Path $root 'VC\vcpkg\vcpkg.exe'
        if (Test-Path $vp) {
            Write-Host ("  VS vcpkg: " + ((& $vp version | Select-Object -First 1) -join ''))
        } else { Write-Host '  VS vcpkg: not found' }
    }
} else {
    Write-Host 'vswhere not found; Visual Studio inventory unavailable.'
}

Write-Host "`n=== Windows SDK include versions ==="
$sdkRoot = Join-Path $pf86 'Windows Kits\10\Include'
if (Test-Path $sdkRoot) {
    Get-ChildItem $sdkRoot -Directory -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Name | ForEach-Object { Write-Host $_ }
} else { Write-Host 'Windows Kits 10 Include directory not found' }

Write-Host "`n=== Local OpenCV/vcpkg pointers (no global changes) ==="
if ($env:VCPKG_ROOT) { Write-Host ("VCPKG_ROOT: " + $env:VCPKG_ROOT) }
else { Write-Host 'VCPKG_ROOT: unset' }
if ($env:OpenCV_DIR) { Write-Host ("OpenCV_DIR: " + $env:OpenCV_DIR) }
else { Write-Host 'OpenCV_DIR: unset' }
Write-Host "`n=== Project baseline to align with ==="
Write-Host 'VS 2026 / MSVC 14.51 / SDK 10.0.26100.0 / CMake >= 4.2 / OpenCV 4.14.0'
Write-Host 'Read-only scan finished; no changes were made.'
