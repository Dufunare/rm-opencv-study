#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char* argv[]) {
    const bool noGui = (argc > 1 && std::string_view(argv[1]) == "--no-gui");
    std::cout << "OpenCV version: " << CV_VERSION << '\n';

    // Self-contained RoboMaster-style image: no camera, video or download needed.
    cv::Mat image(480, 640, CV_8UC3, cv::Scalar(20, 20, 20));
    cv::rectangle(image, cv::Point(215, 165), cv::Point(231, 300), cv::Scalar(255, 110, 40), cv::FILLED);
    cv::rectangle(image, cv::Point(409, 165), cv::Point(425, 300), cv::Scalar(255, 110, 40), cv::FILLED);
    cv::putText(image, "3", cv::Point(298, 272), cv::FONT_HERSHEY_SIMPLEX,
                2.6, cv::Scalar(210, 210, 210), 5, cv::LINE_AA);

    // BGR is OpenCV's standard channel order. The blue bars have B >> R.
    std::vector<cv::Mat> channels;
    cv::split(image, channels);
    cv::Mat blueMinusRed;
    cv::subtract(channels[0], channels[2], blueMinusRed);

    cv::Mat mask;
    cv::threshold(blueMinusRed, mask, 90, 255, cv::THRESH_BINARY);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    cv::Mat annotated = image.clone();
    int lightBars = 0;
    for (const auto& contour : contours) {
        if (cv::contourArea(contour) < 100.0) {
            continue;
        }
        const auto rect = cv::minAreaRect(contour);
        const float small = std::min(rect.size.width, rect.size.height);
        const float large = std::max(rect.size.width, rect.size.height);
        if (small < 1.0f || large / small < 3.0f) {
            continue;
        }
        cv::Point2f cornersF[4];
        rect.points(cornersF);
        std::vector<cv::Point> corners;
        for (const auto& pt : cornersF) {
            corners.emplace_back(cvRound(pt.x), cvRound(pt.y));
        }
        cv::polylines(annotated, std::vector<std::vector<cv::Point>>{corners}, true,
                      cv::Scalar(0, 255, 0), 2);
        ++lightBars;
    }

    const std::filesystem::path outDir = "demo_output";
    std::filesystem::create_directories(outDir);
    const bool saved =
        cv::imwrite((outDir / "source.png").string(), image) &&
        cv::imwrite((outDir / "mask.png").string(), mask) &&
        cv::imwrite((outDir / "detected.png").string(), annotated);

    std::cout << "Light-bar candidates: " << lightBars << '\n';
    std::cout << "Images: " << std::filesystem::absolute(outDir).string() << '\n';
    if (!saved || lightBars != 2) {
        std::cerr << "DEMO FAIL: expected two candidates and three saved images.\n";
        return 1;
    }
    std::cout << "DEMO PASS: two synthetic blue light bars detected.\n";

    if (!noGui) {
        cv::imshow("source", image);
        cv::imshow("mask", mask);
        cv::imshow("detected", annotated);
        std::cout << "Press any key in an image window to close.\n";
        cv::waitKey(0);
    }
    return 0;
}
