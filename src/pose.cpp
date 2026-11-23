#include "pose.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace study {

Camera loadCamera(const std::filesystem::path& path, cv::Size frame) {
    if (path.empty()) {
        const double f = 0.90 * std::max(frame.width, frame.height);
        return {{f, 0, (frame.width - 1) / 2.0, 0, f, (frame.height - 1) / 2.0, 0, 0, 1},
                cv::Mat::zeros(1, 5, CV_64F), "heuristic_focal_0.9x_max_dimension", false};
    }
    cv::FileStorage file(path.u8string(), cv::FileStorage::READ);
    if (!file.isOpened()) {
        throw std::runtime_error("Cannot open camera profile " + path.u8string());
    }
    cv::Size reference(static_cast<int>(file["image_width"]), static_cast<int>(file["image_height"]));
    cv::Mat matrix, distortion;
    std::string source;
    file["camera_matrix"] >> matrix;
    file["distortion_coefficients"] >> distortion;
    file["source"] >> source;
    const auto count = distortion.total();
    if (matrix.rows != 3 || matrix.cols != 3 || matrix.channels() != 1 ||
        distortion.channels() != 1 || (distortion.rows != 1 && distortion.cols != 1) ||
        (count != 4 && count != 5 && count != 8 && count != 12 && count != 14) ||
        !cv::checkRange(matrix) || !cv::checkRange(distortion) || reference.empty() || source.empty()) {
        throw std::runtime_error("Camera profile needs dimensions, a 3x3 matrix, distortion and source");
    }
    matrix.convertTo(matrix, CV_64F);
    distortion.convertTo(distortion, CV_64F);
    cv::Matx33d intrinsics(matrix);
    if (intrinsics(0, 0) <= 0 || intrinsics(1, 1) <= 0 || std::abs(intrinsics(2, 2) - 1) > 1e-9 ||
        intrinsics(0, 1) != 0 || intrinsics(1, 0) != 0 || intrinsics(2, 0) != 0 || intrinsics(2, 1) != 0) {
        throw std::runtime_error("Camera matrix must use positive focal lengths and the OpenCV pinhole layout");
    }
    if (reference != frame) {
        const double sx = static_cast<double>(frame.width) / reference.width;
        const double sy = static_cast<double>(frame.height) / reference.height;
        if (std::abs(sx - sy) > 0.005 * std::max(sx, sy)) {
            throw std::runtime_error("Camera profile aspect ratio differs from media; account for cropping or resizing first");
        }
        intrinsics(0, 0) *= sx;
        intrinsics(1, 1) *= sy;
        intrinsics(0, 2) = (intrinsics(0, 2) + 0.5) * sx - 0.5;
        intrinsics(1, 2) = (intrinsics(1, 2) + 0.5) * sy - 0.5;
    }
    return {intrinsics, distortion, source, static_cast<int>(file["calibrated"]) == 1};
}

std::vector<PlateSize> loadPlateSizes(const std::filesystem::path& path) {
    cv::FileStorage file(path.u8string(), cv::FileStorage::READ);
    if (!file.isOpened()) {
        throw std::runtime_error("Cannot open plate geometry " + path.u8string());
    }
    std::vector<PlateSize> sizes;
    for (const std::string name : {"small", "large"}) {
        const auto node = file[name];
        const double width = node["light_center_spacing_mm"];
        const double height = node["light_length_mm"];
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) {
            throw std::runtime_error("Invalid plate geometry for " + name);
        }
        sizes.push_back({name, width, height});
    }
    return sizes;
}

const PlateSize& chooseSize(const Corners& corners, const std::vector<PlateSize>& sizes,
                           const std::string& mode) {
    if (mode == "small") {
        return sizes.front();
    }
    if (mode == "large") {
        return sizes.back();
    }
    const double ratio = (cv::norm(corners[3] - corners[0]) + cv::norm(corners[2] - corners[1])) /
                         (2 * lightHeight(corners));
    const double midpoint = (sizes.front().widthMm / sizes.front().heightMm +
                             sizes.back().widthMm / sizes.back().heightMm) / 2;
    return ratio < midpoint ? sizes.front() : sizes.back();
}

std::vector<cv::Point3d> objectCorners(const PlateSize& size) {
    const double x = size.widthMm / 2;
    const double y = size.heightMm / 2;
    return {{-x, -y, 0}, {-x, y, 0}, {x, y, 0}, {x, -y, 0}};
}

std::optional<Pose> solvePose(const Corners& corners, const Camera& camera, const PlateSize& size) {
    const auto object = objectCorners(size);
    const std::vector<cv::Point2d> image(corners.begin(), corners.end());
    std::vector<cv::Vec3d> rotations, translations;
    cv::solvePnPGeneric(object, image, camera.matrix, camera.distortion, rotations, translations,
                        false, cv::SOLVEPNP_IPPE);
    std::vector<Pose> valid;
    for (std::size_t i = 0; i < rotations.size(); ++i) {
        const auto rotation = rotations[i];
        const auto translation = translations[i];
        if (!cv::checkRange(cv::Mat(rotation)) || !cv::checkRange(cv::Mat(translation))) {
            continue;
        }
        cv::Matx33d transform;
        cv::Rodrigues(rotation, transform);
        const bool inFront = std::all_of(object.begin(), object.end(), [&](const cv::Point3d& point) {
            return (transform * cv::Vec3d(point) + translation)[2] > 0;
        });
        if (!inFront) {
            continue;
        }
        std::vector<cv::Point2d> projected;
        cv::projectPoints(object, rotation, translation, camera.matrix, camera.distortion, projected);
        const double error = cv::norm(projected, image, cv::NORM_L2) / std::sqrt(image.size());
        if (std::isfinite(error)) {
            valid.push_back({rotation, translation, error, -1});
        }
    }
    std::sort(valid.begin(), valid.end(), [](const Pose& a, const Pose& b) { return a.errorPx < b.errorPx; });
    if (valid.empty() || valid.front().errorPx > std::clamp(0.15 * lightHeight(corners), 2.0, 8.0)) {
        return std::nullopt;
    }
    if (valid.size() > 1) {
        valid.front().alternativeErrorPx = valid[1].errorPx;
    }
    return valid.front();
}

void calibrateCamera(const std::filesystem::path& images, cv::Size board, double squareMm,
                     const std::filesystem::path& output) {
    std::vector<cv::Point3f> pattern;
    for (int y = 0; y < board.height; ++y) {
        for (int x = 0; x < board.width; ++x) {
            pattern.emplace_back(static_cast<float>(x * squareMm), static_cast<float>(y * squareMm), 0.0f);
        }
    }
    std::vector<std::vector<cv::Point2f>> observations;
    std::vector<std::vector<cv::Point3f>> objects;
    cv::Size frame;
    for (const auto& entry : std::filesystem::directory_iterator(images)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const cv::Mat image = readImage(entry.path());
        if (image.empty()) {
            continue;
        }
        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        std::vector<cv::Point2f> corners;
        if (!cv::findChessboardCornersSB(gray, board, corners)) {
            continue;
        }
        if (!frame.empty() && frame != gray.size()) {
            throw std::runtime_error("Calibration images with valid boards must have identical dimensions");
        }
        frame = gray.size();
        observations.push_back(std::move(corners));
        objects.push_back(pattern);
        std::cout << "Chessboard: " << entry.path().filename().u8string() << '\n';
    }
    if (observations.size() < 8) {
        throw std::runtime_error("At least eight usable chessboard views are required");
    }
    cv::Mat matrix, distortion;
    std::vector<cv::Mat> rotations, translations;
    const double error = cv::calibrateCamera(objects, observations, frame, matrix, distortion,
                                              rotations, translations);
    if (!std::isfinite(error) || !cv::checkRange(matrix) || !cv::checkRange(distortion)) {
        throw std::runtime_error("Calibration failed to produce finite parameters");
    }
    std::filesystem::create_directories(std::filesystem::absolute(output).parent_path());
    cv::FileStorage file(output.u8string(), cv::FileStorage::WRITE);
    if (!file.isOpened()) {
        throw std::runtime_error("Cannot write camera profile " + output.u8string());
    }
    file << "image_width" << frame.width << "image_height" << frame.height
         << "camera_matrix" << matrix << "distortion_coefficients" << distortion
         << "source" << ("chessboard:" + images.lexically_normal().u8string()) << "calibrated" << 1
         << "calibration_rms_px" << error << "views" << static_cast<int>(observations.size());
    std::cout << "Calibration RMS: " << error << " px from " << observations.size() << " views\n";
}

void verifyPoseMath() {
    const Camera camera{{1100, 0, 640, 0, 1080, 360, 0, 0, 1},
                        cv::Mat(cv::Vec<double, 5>(-0.075, 0.012, 0.001, -0.001, 0)), "synthetic", true};
    const PlateSize size{"small", 132, 55};
    const cv::Vec3d expectedR(0.24, -0.36, 0.10), expectedT(68, -27, 1640);
    std::vector<cv::Point2d> projected;
    cv::projectPoints(objectCorners(size), expectedR, expectedT, camera.matrix, camera.distortion, projected);
    Corners corners;
    std::copy(projected.begin(), projected.end(), corners.begin());
    const auto pose = solvePose(corners, camera, size);
    if (!pose || cv::norm(pose->positionMm - expectedT) > 0.02 ||
        cv::norm(pose->rotation - expectedR) > 1e-4 || pose->errorPx > 1e-3) {
        throw std::runtime_error("PnP synthetic round-trip failed");
    }
    std::cout << "PnP synthetic round-trip PASS: position error " << cv::norm(pose->positionMm - expectedT)
              << " mm, rotation-vector error " << cv::norm(pose->rotation - expectedR)
              << " rad, reprojection RMS " << pose->errorPx << " px\n";
}

} // namespace study
