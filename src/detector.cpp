#include "detector.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>

namespace study {
namespace {

struct Light {
    cv::Point2f center;
    cv::Point2f axis;
    float length;
    std::string color;
    bool brightCore;
};

void collectLights(const cv::Mat& mask, const std::vector<cv::Mat>& colorMasks, const std::string& color,
                   std::vector<Light>& lights) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto& contour : contours) {
        const auto box = cv::minAreaRect(contour);
        const float length = std::max(box.size.width, box.size.height);
        const float width = std::min(box.size.width, box.size.height);
        if (length < 4 || width < 1 || length < 1.65f * width || length > mask.rows * 0.65f ||
            cv::contourArea(contour) < 0.33 * length * width) {
            continue;
        }
        const float angle = (box.angle + (box.size.width < box.size.height ? 90 : 0)) *
                            static_cast<float>(CV_PI / 180);
        cv::Point2f axis(std::cos(angle), std::sin(angle));
        if (axis.y < 0) {
            axis *= -1;
        }
        if (axis.y < 0.66f) {
            continue;
        }
        if (!color.empty()) {
            const bool duplicatesCore = std::any_of(lights.begin(), lights.end(), [&](const Light& prior) {
                return prior.brightCore && prior.color == color && prior.axis.dot(axis) > 0.93f &&
                       cv::norm(prior.center - box.center) < 0.34 * std::max(prior.length, length) &&
                       std::min(prior.length, length) > 0.54f * std::max(prior.length, length);
            });
            if (!duplicatesCore) {
                lights.push_back({box.center, axis, length, color, false});
            }
            continue;
        }
        const auto bounds = box.boundingRect();
        const int padding = std::max(2, cvRound(width * 0.85f));
        const auto roi = (bounds + cv::Size(padding * 2, padding * 2) - cv::Point(padding, padding)) &
                         cv::Rect(0, 0, mask.cols, mask.rows);
        const int blue = cv::countNonZero(colorMasks[0](roi));
        const int red = cv::countNonZero(colorMasks[1](roi));
        if (std::max(blue, red) >= std::max(4.0, 0.32 * length * width) &&
            std::max(blue, red) > 1.4 * std::min(blue, red)) {
            lights.push_back({box.center, axis, length, blue > red ? "blue" : "red", true});
        }
    }
}

cv::Mat stickerFeature(const cv::Mat& image, const Corners& corners) {
    if (lightHeight(corners) < 5 || !cv::isContourConvex(std::vector<cv::Point2f>(corners.begin(), corners.end()))) {
        return {};
    }
    // The printed glyph commonly extends above and below the light endpoints.
    const Corners canonical{{{0, 18}, {0, 49}, {65, 49}, {65, 18}}};
    const cv::Mat transform = cv::getPerspectiveTransform(corners.data(), canonical.data());
    cv::Mat patch;
    cv::warpPerspective(image, patch, transform, {66, 68}, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    patch = patch(cv::Rect(12, 1, 42, 66));
    cv::cvtColor(patch, patch, cv::COLOR_BGR2GRAY);
    cv::resize(patch, patch, {32, 48}, 0, 0, cv::INTER_AREA);
    cv::normalize(patch, patch, 0, 255, cv::NORM_MINMAX);
    const cv::HOGDescriptor hog({32, 48}, {16, 16}, {8, 8}, {8, 8}, 9);
    std::vector<float> features;
    hog.compute(patch, features);
    cv::Mat row(features, true);
    row = row.reshape(1, 1);
    return row;
}

} // namespace

cv::Mat readImage(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    return bytes.empty() ? cv::Mat() : cv::imdecode(bytes, cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
}

void saveImage(const std::filesystem::path& path, const cv::Mat& image) {
    std::vector<unsigned char> bytes;
    if (!cv::imencode(".png", image, bytes)) {
        throw std::runtime_error("PNG encoder failed");
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("Cannot write " + path.u8string());
    }
}

cv::Point2f centerOf(const Corners& corners) {
    return (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
}

float lightHeight(const Corners& corners) {
    return static_cast<float>((cv::norm(corners[1] - corners[0]) + cv::norm(corners[2] - corners[3])) / 2);
}

std::vector<Plate> detectPlates(const cv::Mat& image, const StickerModel& model) {
    std::vector<cv::Mat> channels;
    cv::split(image, channels);
    std::vector<Light> lights;
    std::vector<cv::Mat> colorMasks;
    for (int index : {0, 2}) {
        const auto& dominant = channels[index];
        cv::Mat difference;
        cv::subtract(dominant, channels[2 - index], difference);
        cv::Mat mask = (difference >= 35) & (dominant >= 115);
        colorMasks.push_back(std::move(mask));
    }
    cv::Mat peak;
    cv::max(channels[0], channels[2], peak);
    const cv::Mat core = (channels[1] >= 175) & (peak >= 219);
    collectLights(core, colorMasks, "", lights);
    const auto kernel = cv::getStructuringElement(cv::MORPH_RECT, {3, 3});
    for (std::size_t index = 0; index < colorMasks.size(); ++index) {
        cv::Mat mask;
        cv::morphologyEx(colorMasks[index], mask, cv::MORPH_CLOSE, kernel);
        collectLights(mask, colorMasks, index == 0 ? "blue" : "red", lights);
    }

    struct Pair {
        Plate plate;
        std::size_t left;
        std::size_t right;
        double ranking;
    };
    std::vector<Pair> candidates;
    std::sort(lights.begin(), lights.end(), [](const Light& a, const Light& b) {
        return a.center.x < b.center.x;
    });
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const auto& a = lights[i];
        for (std::size_t j = i + 1; j < lights.size(); ++j) {
            const auto& b = lights[j];
            const auto delta = b.center - a.center;
            if (delta.x > a.length * 7) {
                break;
            }
            const float lengthRatio = std::min(a.length, b.length) / std::max(a.length, b.length);
            const float height = (a.length + b.length) * 0.5f;
            const float separation = static_cast<float>(cv::norm(delta)) / height;
            const auto axis = (a.axis + b.axis) * 0.5f;
            const float alongBar = std::abs(delta.dot(axis)) / height;
            const float parallel = a.axis.dot(b.axis);
            if (a.color != b.color || height < 7 || lengthRatio < 0.58f || separation < 0.94f || separation > 5.6f ||
                alongBar > 1.05f || parallel < 0.86f || std::abs(delta.y) > 0.8f * delta.x) {
                continue;
            }
            bool enclosed = false;
            for (std::size_t k = i + 1; k < j; ++k) {
                const auto& other = lights[k];
                const float fraction = (other.center.x - a.center.x) / delta.x;
                const float offset = other.center.y - (a.center.y + fraction * delta.y);
                if (other.color == a.color && std::abs(offset) < height * 0.35f && other.length > height * 0.55f) {
                    enclosed = true;
                    break;
                }
            }
            if (enclosed) {
                continue;
            }
            const auto left = a.axis * (a.length * 0.5f);
            const auto right = b.axis * (b.length * 0.5f);
            const double cost = (1 - lengthRatio) + 0.65 * alongBar + 2 * (1 - parallel) +
                                0.12 * (!a.brightCore + !b.brightCore) +
                                0.13 * std::min(std::abs(separation - 2.4f), std::abs(separation - 4.1f));
            Plate plate{{a.center - left, a.center + left, b.center + right, b.center - right},
                        a.color, 1 / (1 + cost)};
            if (plate.score < 0.53 || !cv::isContourConvex(std::vector<cv::Point2f>(plate.corners.begin(), plate.corners.end()))) {
                continue;
            }
            model.classify(image, plate);
            if (plate.score < 0.86 && (plate.stickerDistance < 0 || plate.stickerDistance > 2.27)) {
                continue;
            }
            const double ranking = plate.score - 0.10 * std::max(0.0, plate.stickerDistance - 1);
            candidates.push_back({std::move(plate), i, j, ranking});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Pair& a, const Pair& b) {
        return a.ranking > b.ranking;
    });
    std::vector<Plate> detections;
    std::vector<bool> used(lights.size(), false);
    for (auto& pair : candidates) {
        if (!used[pair.left] && !used[pair.right]) {
            used[pair.left] = used[pair.right] = true;
            detections.push_back(std::move(pair.plate));
        }
    }
    return detections;
}

void StickerModel::load(const std::filesystem::path& path) {
    cv::FileStorage file(path.u8string(), cv::FileStorage::READ);
    if (!file.isOpened()) {
        throw std::runtime_error("Cannot open sticker model " + path.u8string());
    }
    file["features"] >> features_;
    file["labels"] >> labels_;
    const cv::HOGDescriptor hog({32, 48}, {16, 16}, {8, 8}, {8, 8}, 9);
    if (features_.rows < 3 || features_.cols != static_cast<int>(hog.getDescriptorSize()) ||
        features_.type() != CV_32F || labels_.size() != static_cast<std::size_t>(features_.rows) ||
        !cv::checkRange(features_)) {
        throw std::runtime_error("Invalid sticker model format");
    }
}

void StickerModel::classify(const cv::Mat& image, Plate& plate) const {
    if (features_.empty()) {
        return;
    }
    const cv::Mat query = stickerFeature(image, plate.corners);
    if (query.empty()) {
        return;
    }
    cv::Mat distances, neighbors;
    cv::batchDistance(query, features_, distances, CV_32F, neighbors, cv::NORM_L2, std::min(7, features_.rows));
    std::map<std::string, double> votes;
    for (int i = 0; i < neighbors.cols; ++i) {
        const double distance = distances.at<float>(0, i);
        votes[labels_[neighbors.at<int>(0, i)]] += 1 / std::max(0.2, distance);
    }
    const auto winner = std::max_element(votes.begin(), votes.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    double total = 0;
    for (const auto& vote : votes) {
        total += vote.second;
    }
    plate.stickerDistance = distances.at<float>(0, 0);
    if (winner->second / total >= 0.44 && plate.stickerDistance <= 3.3) {
        plate.sticker = winner->first;
    }
}

void StickerModel::train(const std::filesystem::path& manifest, const std::filesystem::path& output) {
    std::ifstream input(manifest);
    if (!input) {
        throw std::runtime_error("Cannot open training manifest " + manifest.u8string());
    }
    cv::Mat features;
    std::vector<std::string> labels;
    std::map<std::string, int> classCounts;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string imagePath, label;
        Corners corners;
        std::getline(row, imagePath, '\t');
        std::getline(row, label, '\t');
        for (auto& point : corners) {
            row >> point.x >> point.y;
        }
        if (!row || label.empty()) {
            throw std::runtime_error("Invalid training row: " + line);
        }
        const auto image = readImage(manifest.parent_path() / std::filesystem::u8path(imagePath));
        if (image.empty()) {
            throw std::runtime_error("Cannot decode training image " + imagePath);
        }
        const auto feature = stickerFeature(image, corners);
        if (!feature.empty()) {
            features.push_back(feature);
            labels.push_back(label);
            ++classCounts[label];
        }
    }
    if (classCounts.size() < 2 || features.rows < 3) {
        throw std::runtime_error("Training requires at least two represented classes and three samples");
    }
    std::filesystem::create_directories(std::filesystem::absolute(output).parent_path());
    cv::FileStorage file(output.u8string(), cv::FileStorage::WRITE);
    if (!file.isOpened()) {
        throw std::runtime_error("Cannot write sticker model " + output.u8string());
    }
    file << "features" << features << "labels" << labels;
    for (const auto& [label, count] : classCounts) {
        std::cout << label << ": " << count << '\n';
    }
    std::cout << "Trained " << features.rows << " HOG templates: " << output.u8string() << '\n';
}

} // namespace study
