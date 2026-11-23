#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace study {

// Left top, left bottom, right bottom, right top (sjtu4).
using Corners = std::array<cv::Point2f, 4>;

struct Plate {
    Corners corners;
    std::string color;
    double score;
    std::string sticker = "unknown";
    double stickerDistance = -1;
};

class StickerModel;

cv::Mat readImage(const std::filesystem::path& path);
void saveImage(const std::filesystem::path& path, const cv::Mat& image);
std::vector<Plate> detectPlates(const cv::Mat& image, const StickerModel& model);
cv::Point2f centerOf(const Corners& corners);
float lightHeight(const Corners& corners);

class StickerModel {
public:
    void load(const std::filesystem::path& path);
    void classify(const cv::Mat& image, Plate& plate) const;
    static void train(const std::filesystem::path& manifest, const std::filesystem::path& output);

private:
    cv::Mat features_;
    std::vector<std::string> labels_;
};

} // namespace study
