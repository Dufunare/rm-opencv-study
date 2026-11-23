# First build acceptance checklist

The external environment inventory is not a substitute for running the local inspection and build.

- [x] Desktop: read-only inspection reports VS2026 18.11.12328.437, MSVC 14.51, SDK 10.0.26100.0
- [x] Desktop: confirm bundled vcpkg CMake integration file exists
- [x] Desktop: CMake configure finds the existing manifest installation of exact OpenCV 4.14.0
- [x] Desktop: configuration log confirms v145 / 14.51 / SDK 10.0.26100.0
- [ ] Desktop: Debug build passes
- [x] Desktop: executable prints `DEMO PASS` and saves three images
- [ ] Desktop: GUI renders expected light bars
- [ ] Git: source pushed; local presets and build products untracked
- [ ] Laptop: read-only inspection reviewed and missing components added side-by-side as needed
- [ ] Laptop: same CMake preset and OpenCV version; Demo passes

## Assessment verification

- [x] Desktop: Debug and Release builds of `armor_study` succeed with the existing OpenCV installation.
- [x] Known-intrinsics PnP round trip and synthetic chessboard calibration pass.
- [x] Number templates train from the cached dataset with the evaluation sequences held out.
- [x] A blue 3 plate image and both user-supplied videos produce readable annotations and CSV files. The video preview opens and processes the first frames.
- [x] The evaluation script runs the 20 labeled images, and the video runs compare forecasts with later observations.
- [x] The YOLO Pose export generates the image and label split. Model training is a separate experiment.

See [the assessment guide](ASSESSMENT.md) for commands, measurements and input assumptions.
