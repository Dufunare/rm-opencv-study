#pragma once

#include "detector.hpp"

#include <optional>

namespace study {

struct Camera {
    cv::Matx33d matrix;
    cv::Mat distortion;
    std::string source;
    bool calibrated;
};

struct PlateSize {
    std::string name;
    double widthMm;
    double heightMm;
};

struct Pose {
    cv::Vec3d rotation;
    cv::Vec3d positionMm;
    double errorPx;
    double alternativeErrorPx;
};

Camera loadCamera(const std::filesystem::path& path, cv::Size frame);
std::vector<PlateSize> loadPlateSizes(const std::filesystem::path& path);
const PlateSize& chooseSize(const Corners& corners, const std::vector<PlateSize>& sizes,
                           const std::string& mode);
std::vector<cv::Point3d> objectCorners(const PlateSize& size);
std::optional<Pose> solvePose(const Corners& corners, const Camera& camera, const PlateSize& size);
void calibrateCamera(const std::filesystem::path& images, cv::Size board, double squareMm,
                     const std::filesystem::path& output);
void verifyPoseMath();

} // namespace study
