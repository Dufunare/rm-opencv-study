# RoboMaster OpenCV study — C++ / Visual Studio 2026

`armor_study` reads recorded images and videos, labels red and blue plates, estimates pose, and compares constant-velocity forecasts with later video observations. [考核演示与答辩说明](docs/ASSESSMENT.md) covers the run commands, results, camera assumptions, and YOLO Pose experiment.

From the repository root, run the Release executable on the prepared materials:

```powershell
$study = '.\out\build\windows-vs2026-x64\Release\armor_study.exe'
& $study --self-test
& $study --image datas/samples/mixed_armor/mixed_armor_000261.jpg --out demo_output/assessment/image
& $study --video 'datas/videos/远距离平移.avi' --forecast-frames 3 --max-frames 450 --out demo_output/assessment/far
```

These commands expect `out/models/stickers.yml.gz`. The assessment guide includes the `uv` and OpenCV commands to rebuild it from the pinned dataset.

## Project baseline

| Item | Value |
| --- | --- |
| Compiler | MSVC 14.51, Visual Studio 2026 `v145` |
| Windows SDK | `10.0.26100.0` |
| CMake | >= 4.2; desktop currently ships 4.3.1-msvc1 |
| C++ | C++17 |
| OpenCV | 4.14.0 (`find_package` requires exactly 4.14.0) |
| Dependency declarations | vcpkg manifest with registry baseline `e54ef0fcc9a82033a61e51002552a910a5d57282` |
| vcpkg executable | **Reuse Visual Studio 2026's bundled vcpkg**; no separate checkout or bootstrap |
| vcpkg triplet | `x64-windows-v145-1451` (dynamic CRT/library) |

The baseline pins the *dependency registry*; it does not pin the independent `vcpkg.exe` tool binary itself. The desktop's shipped vcpkg is suitable for this project; on the laptop, validate the installed vcpkg tool and package results. Local paths are written only to ignored `CMakeUserPresets.json`.

## Desktop — first build

1. Unzip this project to `D:\Dev\projects\rm-opencv-study` (or another directory).
2. Open Windows PowerShell at the project root:

   ```powershell
   .\scripts\Inspect-Windows.ps1
   .\scripts\Setup-Project.ps1 -Build
   ```

The setup script reads the existing VS2026 instance, MSVC and SDK. It **does not** clone or bootstrap vcpkg or change PATH. It checks that the VS bundled `vcpkg.exe` and `scripts/buildsystems/vcpkg.cmake` exist, then creates the ignored user presets and performs configure/build/test using the VS CMake executable by full path. The first build may still download/compile **OpenCV and other packages**.

Successful output should include `OpenCV version: 4.14.0`, `Light-bar candidates: 2`, `DEMO PASS`; the demo creates `demo_output/source.png`, `mask.png`, and `detected.png`.

After successful build, display the GUI with:

```powershell
.\out\build\windows-vs2026-x64\Debug\armor_demo.exe
```

If PowerShell script policy blocks execution, use a process-scoped invocation:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Setup-Project.ps1 -Build
```

## Laptop — check first, then align

Clone the same repository and run the **read-only** diagnostic:

```powershell
.\scripts\Inspect-Windows.ps1
```

Inspect the results before changing any installation. Keep existing older/newer toolchains; install the *missing* required MSVC/SDK/VS features side by side as necessary. Run `Setup-Project.ps1 -Build` only after the baseline is satisfied.

If a laptop has a usable standalone vcpkg but lacks the VS-bundled CMake integration file, you may pass `-VcpkgRoot '<actual-path>'`. Do not set a global VCPKG_ROOT; a per-machine CMakeUserPresets.json is sufficient.

## Git workflow

Commit source, presets, manifests and docs. Do not commit generated `CMakeUserPresets.json`, output binaries, `vcpkg_installed`, or images. Both PCs will independently restore the same OpenCV 4.14.0 dependency selection from the project's manifest.

## VS and VS Code

Visual Studio: **File -> Open -> Folder**, select repository root, then CMake preset `windows-vs2026-x64`.
VS Code: install Microsoft **C/C++** and **CMake Tools** extensions and select the same preset. For command-line builds, VS bundled CMake is used even when `cmake` is not on PATH.

## Synthetic build smoke test

`armor_demo` generates a synthetic BGR image with two blue light bars and a white number 3. It uses blue-minus-red channel difference, thresholding, `findContours` and `minAreaRect` to verify the build without media or a model. The assessment entry point is `armor_study`.

## Sources

- https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode
- https://learn.microsoft.com/en-us/vcpkg/concepts/registries
- https://learn.microsoft.com/en-us/vcpkg/consume/manifest-mode
- https://learn.microsoft.com/en-us/vcpkg/troubleshoot/build-failures
