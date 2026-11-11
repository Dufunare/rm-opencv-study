# First build acceptance checklist

The external environment inventory is not a substitute for running the local inspection and build.

- [x] Desktop: read-only inspection reports VS2026 18.11.12328.437, MSVC 14.51, SDK 10.0.26100.0
- [ ] Desktop: confirm bundled vcpkg CMake integration file exists
- [ ] Desktop: CMake configure completes using VS vcpkg manifest baseline and exact OpenCV 4.14.0
- [ ] Desktop: configuration log confirms v145 / 14.51 / SDK 10.0.26100.0
- [ ] Desktop: Debug build passes
- [ ] Desktop: executable prints `DEMO PASS` and saves three images
- [ ] Desktop: GUI renders expected light bars
- [ ] Git: source pushed; local presets and build products untracked
- [ ] Laptop: read-only inspection reviewed and missing components added side-by-side as needed
- [ ] Laptop: same CMake preset and OpenCV version; Demo passes

Current status: **not yet compiled or run on the user's Windows machines**.
