#include "detector.hpp"
#include "forecast.hpp"
#include "pose.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;
using namespace study;

const char* window = "OpenCV plate study";

struct Options {
    fs::path image, video, training, calibration, camera;
    fs::path output = "demo_output/study";
    fs::path geometry = "config/plates.yml";
    fs::path model = "out/models/stickers.yml.gz";
    double squareMm = 0;
    cv::Size board;
    std::string size = "auto";
    int firstFrame = 0, maxFrames = 0, forecastFrames = 0;
    bool headless = false, saveVideo = false, verify = false, help = false;
};

struct Result {
    Plate plate;
    std::string size;
    std::optional<Pose> pose;
};

void usage() {
    std::cout <<
        "armor_study --image FILE|--video FILE [--out DIR] [--no-gui]\n"
        "  --camera YAML  --geometry YAML  --plate-size auto|small|large\n"
        "  --number-model YAML[.gz]   default out/models/stickers.yml.gz\n"
        "  Video: --start-frame N --max-frames N --forecast-frames N --write-video\n"
        "armor_study --train MANIFEST --number-model YAML[.gz]\n"
        "armor_study --calibrate DIR --board COLSxROWS --square-mm MM --out CAMERA.yml\n"
        "armor_study --self-test\n"
        "Video preview: Space pauses; Q or Escape exits.\n";
}

int readInt(std::string_view value) {
    int n = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), n);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || n < 0) {
        throw std::runtime_error("Expected a nonnegative integer: " + std::string(value));
    }
    return n;
}

Options parse(const std::vector<std::string>& arguments) {
    Options options;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& flag = arguments[i];
        if (flag == "--no-gui") { options.headless = true; continue; }
        if (flag == "--write-video") { options.saveVideo = true; continue; }
        if (flag == "--self-test") { options.verify = true; continue; }
        if (flag == "--help" || flag == "-h") { options.help = true; continue; }
        if (++i == arguments.size()) {
            throw std::runtime_error("Missing value for " + flag);
        }
        const auto& value = arguments[i];
        if (flag == "--image") options.image = fs::u8path(value);
        else if (flag == "--video") options.video = fs::u8path(value);
        else if (flag == "--train") options.training = fs::u8path(value);
        else if (flag == "--calibrate") options.calibration = fs::u8path(value);
        else if (flag == "--out") options.output = fs::u8path(value);
        else if (flag == "--camera") options.camera = fs::u8path(value);
        else if (flag == "--geometry") options.geometry = fs::u8path(value);
        else if (flag == "--number-model") options.model = fs::u8path(value);
        else if (flag == "--plate-size") options.size = value;
        else if (flag == "--start-frame") options.firstFrame = readInt(value);
        else if (flag == "--max-frames") options.maxFrames = readInt(value);
        else if (flag == "--forecast-frames") options.forecastFrames = readInt(value);
        else if (flag == "--board") {
            const auto x = value.find('x');
            if (x == std::string::npos) {
                throw std::runtime_error("Board format is COLSxROWS (inner corners)");
            }
            options.board = {readInt(value.substr(0, x)), readInt(value.substr(x + 1))};
        } else if (flag == "--square-mm") {
            std::size_t end;
            options.squareMm = std::stod(value, &end);
            if (end != value.size() || !std::isfinite(options.squareMm)) {
                throw std::runtime_error("Invalid chessboard square size");
            }
        } else {
            throw std::runtime_error("Unknown option: " + flag);
        }
    }
    if (options.help) {
        return options;
    }
    if (static_cast<int>(!options.image.empty()) + !options.video.empty() + !options.training.empty() +
        !options.calibration.empty() + options.verify != 1) {
        throw std::runtime_error("Select exactly one run mode. See --help.");
    }
    if (options.size != "auto" && options.size != "small" && options.size != "large") {
        throw std::runtime_error("Plate size must be auto, small or large");
    }
    if ((options.firstFrame || options.maxFrames || options.forecastFrames || options.saveVideo) &&
        options.video.empty()) {
        throw std::runtime_error("Frame and video options require --video");
    }
    if (options.forecastFrames > 300) {
        throw std::runtime_error("Forecast horizon must be 0 to 300 frames");
    }
    return options;
}

std::string csvText(const std::string& text) {
    std::string escaped = "\"";
    for (const char value : text) {
        escaped += value;
        if (value == '"') {
            escaped += '"';
        }
    }
    return escaped + '"';
}

void vectorCsv(std::ostream& out, const cv::Vec3d& vector) {
    out << ',' << vector[0] << ',' << vector[1] << ',' << vector[2];
}

std::string vectorText(const cv::Vec3d& vector, int digits) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(digits) << vector[0] << ' ' << vector[1] << ' ' << vector[2];
    return out.str();
}

std::ofstream resultFile(const fs::path& path) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot write " + path.u8string());
    }
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file << std::fixed << std::setprecision(6);
    return file;
}

void label(cv::Mat& image, const std::string& text, cv::Point start, double scale, cv::Scalar color) {
    int baseline;
    const auto size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, scale, 1, &baseline);
    start.x = std::clamp(start.x, 3, std::max(3, image.cols - size.width - 4));
    start.y = std::clamp(start.y, size.height + 3, std::max(size.height + 3, image.rows - baseline - 2));
    cv::rectangle(image, start + cv::Point(-2, -size.height - 2), start + cv::Point(size.width + 2, baseline),
                  {15, 15, 15}, cv::FILLED);
    cv::putText(image, text, start, cv::FONT_HERSHEY_SIMPLEX, scale, color, 1, cv::LINE_AA);
}

void drawPlate(cv::Mat& image, const Result& result, const Camera& camera) {
    const auto& plate = result.plate;
    const cv::Scalar color = plate.color == "blue" ? cv::Scalar(255, 180, 65) : cv::Scalar(70, 105, 255);
    const std::vector<cv::Point> outline(plate.corners.begin(), plate.corners.end());
    cv::polylines(image, outline, true, color, 2, cv::LINE_AA);
    for (std::size_t i = 0; i < outline.size(); ++i) {
        cv::circle(image, outline[i], 3, {240, 240, 240}, cv::FILLED, cv::LINE_AA);
        label(image, std::to_string(i), outline[i] + cv::Point(i < 2 ? -12 : 4, i == 0 || i == 3 ? -4 : 12),
              0.35, {245, 245, 245});
    }
    const cv::Point center = centerOf(plate.corners);
    int y = std::max(outline[1].y, outline[2].y) + 21;
    if (y + 52 >= image.rows) {
        y = std::min(outline[0].y, outline[3].y) - 54;
    }
    const int x = center.x - 22;
    label(image, plate.color + " #" + plate.sticker + " " + result.size, {x, y}, 0.5, color);
    if (result.pose) {
        const auto& pose = *result.pose;
        cv::drawFrameAxes(image, camera.matrix, camera.distortion, pose.rotation, pose.positionMm, 31, 2);
        label(image, "t[m] " + vectorText(pose.positionMm / 1000, 2), {x, y + 19}, 0.44, {235, 235, 235});
        label(image, "r[rad] " + vectorText(pose.rotation, 2), {x, y + 36}, 0.44, {235, 235, 235});
    } else {
        label(image, "pose rejected", {x, y + 19}, 0.44, {215, 215, 215});
    }
}

void drawForecast(cv::Mat& image, const Camera& camera, const Observation& observation, const Forecast& prediction) {
    if (prediction.position[2] <= 0) {
        return;
    }
    const std::vector<cv::Point3d> position{cv::Point3d(prediction.position)};
    std::vector<cv::Point2d> projected;
    cv::projectPoints(position, cv::Vec3d(), cv::Vec3d(), camera.matrix, camera.distortion, projected);
    const auto point = projected.front();
    if (point.x >= 0 && point.y >= 0 && point.x < image.cols && point.y < image.rows) {
        cv::line(image, observation.pixel, point, {0, 235, 255}, 1, cv::LINE_AA);
        cv::drawMarker(image, point, {0, 235, 255}, cv::MARKER_CROSS, 18, 2, cv::LINE_AA);
    }
}

void writeResult(std::ostream& csv, int frame, double seconds, std::size_t index, const Result& result) {
    const auto& plate = result.plate;
    csv << frame << ',' << seconds << ',' << index << ',' << plate.color << ',' << csvText(plate.sticker)
        << ',' << result.size << ',' << plate.score << ',' << plate.stickerDistance << ',' << result.pose.has_value();
    for (const auto& p : plate.corners) {
        csv << ',' << p.x << ',' << p.y;
    }
    if (result.pose) {
        vectorCsv(csv, result.pose->positionMm);
        vectorCsv(csv, result.pose->rotation);
        csv << ',' << result.pose->errorPx << ',' << result.pose->alternativeErrorPx;
    } else {
        csv << ",,,,,,,,";
    }
    csv << '\n';
}

bool preview(const cv::Mat& image, int delay, bool video) {
    cv::imshow(window, image);
    int key = cv::waitKey(delay);
    if (video && key == ' ') {
        do {
            key = cv::waitKey(60);
        } while (key == -1 && cv::getWindowProperty(window, cv::WND_PROP_VISIBLE) >= 1);
    }
    return key != 27 && key != 'q' && key != 'Q' && cv::getWindowProperty(window, cv::WND_PROP_VISIBLE) >= 1;
}

void checkOutput(const fs::path& output, const std::vector<fs::path>& inputs) {
    for (const auto& input : inputs) {
        if (!input.empty() && fs::exists(output) && fs::exists(input) && fs::equivalent(input, output)) {
            throw std::runtime_error("Output would overwrite input " + input.u8string());
        }
    }
}

class VideoInputPath {
public:
    explicit VideoInputPath(const fs::path& original) : original_(original) {}
    VideoInputPath(const VideoInputPath&) = delete;
    VideoInputPath& operator=(const VideoInputPath&) = delete;

    ~VideoInputPath() {
        std::error_code ignored;
        if (!staged_.empty()) {
            fs::remove(staged_, ignored);
        }
        if (!directory_.empty()) {
            fs::remove(directory_, ignored);
        }
    }

    bool open(cv::VideoCapture& capture) {
#ifdef _WIN32
        // Media Foundation interprets filenames in the Windows system code page.
        std::string native;
        try {
            native = original_.string();
        } catch (const std::system_error&) {
            native = stage().string();
        }
        if (capture.open(native, cv::CAP_MSMF)) {
            return true;
        }
        if (!staged_.empty()) {
            return capture.open(native);
        }
#endif
        return capture.open(original_.u8string());
    }

private:
    fs::path stage() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::create_directories("out");
        for (int i = 0; i < 16; ++i) {
            const auto candidate = fs::path("out") / (".video-input-" + std::to_string(stamp) + "-" + std::to_string(i));
            if (fs::create_directory(candidate)) {
                directory_ = candidate;
                break;
            }
        }
        if (directory_.empty()) {
            throw std::runtime_error("Could not reserve a temporary directory for video decoding");
        }
        auto extension = original_.extension().u8string();
        if (extension.find_first_not_of(".abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != std::string::npos) {
            extension.clear();
        }
        staged_ = directory_ / ("clip" + extension);
        std::error_code linkError;
        fs::create_hard_link(original_, staged_, linkError);
        if (linkError) {
            fs::copy_file(original_, staged_);
        }
        return staged_;
    }

    fs::path original_, directory_, staged_;
};

void runMedia(const Options& options) {
    const bool video = !options.video.empty();
    VideoInputPath videoPath(options.video);
    cv::VideoCapture input;
    cv::Mat frame;
    double fps = 0;
    if (video) {
        if (!fs::is_regular_file(options.video) || !videoPath.open(input)) {
            throw std::runtime_error("Cannot open video " + options.video.u8string());
        }
        std::cout << "Video backend: " << input.getBackendName() << '\n';
        fps = input.get(cv::CAP_PROP_FPS);
        if (!std::isfinite(fps) || fps <= 0) {
            throw std::runtime_error("Video backend did not provide a usable frame rate");
        }
        for (int i = 0; i < options.firstFrame; ++i) {
            if (!input.grab()) {
                throw std::runtime_error("Requested start frame is past the end of the video");
            }
        }
        input.read(frame);
    } else {
        frame = readImage(options.image);
    }
    if (frame.empty()) {
        throw std::runtime_error("Could not decode the first requested frame");
    }
    const auto camera = loadCamera(options.camera, frame.size());
    const auto sizes = loadPlateSizes(options.geometry);
    StickerModel model;
    model.load(options.model);
    fs::create_directories(options.output);
    const std::vector<fs::path> inputs{options.image, options.video, options.camera, options.geometry, options.model};
    std::vector<std::string> outputFiles{"annotated.png", "detections.csv", "run.txt"};
    if (options.forecastFrames) {
        outputFiles.emplace_back("forecast.csv");
    }
    if (options.saveVideo) {
        outputFiles.emplace_back("annotated.avi");
    }
    for (const auto& filename : outputFiles) {
        checkOutput(options.output / filename, inputs);
    }
    auto csv = resultFile(options.output / "detections.csv");
    csv << "frame,time_s,detection,color,sticker,size,geometry_score,sticker_l2,pose_ok,"
           "lt_x,lt_y,lb_x,lb_y,rb_x,rb_y,rt_x,rt_y,tx_mm,ty_mm,tz_mm,rx_rad,ry_rad,rz_rad,reproj_rms_px,alt_rms_px\n";
    auto report = resultFile(options.output / "run.txt");
    std::ostringstream metadata;
    metadata << "OpenCV " << CV_VERSION << '\n'
             << "input=" << (video ? options.video : options.image).u8string() << '\n'
             << "resolution=" << frame.cols << 'x' << frame.rows << " fps=" << fps << '\n'
             << "camera=" << camera.source << " calibrated=" << camera.calibrated << '\n'
             << "K=" << cv::Mat(camera.matrix) << " D=" << camera.distortion << '\n'
             << "geometry=" << options.geometry.u8string() << " size_mode=" << options.size << '\n'
             << "model=" << options.model.u8string() << '\n';
    std::cout << metadata.str();
    report << metadata.str();

    std::ofstream forecastLog;
    MotionStudy motion(options.forecastFrames, fps);
    if (options.forecastFrames) {
        forecastLog = resultFile(options.output / "forecast.csv");
        forecastLog << "origin_frame,comparison_frame,horizon_s,vx_mm_s,vy_mm_s,vz_mm_s,"
                       "pred_x_mm,pred_y_mm,pred_z_mm,obs_x_mm,obs_y_mm,obs_z_mm,error_mm,hold_error_mm\n";
    }
    cv::VideoWriter writer;
    if (options.saveVideo) {
        if (!writer.open((options.output / "annotated.avi").u8string(), cv::CAP_OPENCV_MJPEG,
                         cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, frame.size())) {
            throw std::runtime_error("Could not open annotated AVI output");
        }
    }
    if (!options.headless) {
        cv::namedWindow(window, cv::WINDOW_NORMAL | cv::WINDOW_KEEPRATIO);
        const double scale = std::min({1.0, 1200.0 / frame.cols, 760.0 / frame.rows});
        cv::resizeWindow(window, cvRound(frame.cols * scale), cvRound(frame.rows * scale));
    }

    cv::Mat snapshot;
    bool snapshotHasPose = false;
    int snapshotFrame = options.firstFrame;
    int frames = 0, framesWithPlates = 0, detections = 0, poses = 0, comparisons = 0, numbered = 0;
    double predictionError = 0, holdError = 0;
    do {
        const int frameNo = options.firstFrame + frames;
        const double seconds = video ? frameNo / fps : 0;
        auto plates = detectPlates(frame, model);
        cv::Mat annotated = frame.clone();
        std::vector<Observation> observations;
        framesWithPlates += !plates.empty();
        detections += static_cast<int>(plates.size());
        for (std::size_t i = 0; i < plates.size(); ++i) {
            auto& plate = plates[i];
            numbered += plate.sticker != "unknown";
            const auto& size = chooseSize(plate.corners, sizes, options.size);
            Result result{std::move(plate), size.name, std::nullopt};
            result.pose = solvePose(result.plate.corners, camera, size);
            if (result.pose) {
                ++poses;
                observations.push_back({frameNo, seconds, centerOf(result.plate.corners), result.plate.color,
                    result.size, lightHeight(result.plate.corners), result.pose->positionMm});
            }
            writeResult(csv, frameNo, seconds, i, result);
            drawPlate(annotated, result, camera);
        }
        const std::string status = "frame " + std::to_string(frameNo) + " | plates " + std::to_string(plates.size()) +
            " | " + (camera.calibrated ? "calibrated camera" : "approximate camera");
        label(annotated, status, {9, 21}, 0.53, {245, 245, 245});
        if (options.forecastFrames) {
            const auto observation = motion.associate(observations);
            const auto prediction = motion.observe(observation);
            if (prediction && observation) {
                const double error = cv::norm(prediction->position - observation->position);
                const double baseline = cv::norm(prediction->originPosition - observation->position);
                predictionError += error;
                holdError += baseline;
                ++comparisons;
                forecastLog << prediction->originFrame << ',' << prediction->evaluationFrame << ',' << prediction->seconds;
                vectorCsv(forecastLog, prediction->velocity);
                vectorCsv(forecastLog, prediction->position);
                vectorCsv(forecastLog, observation->position);
                forecastLog << ',' << error << ',' << baseline << '\n';
                drawForecast(annotated, camera, *observation, *prediction);
                std::ostringstream text;
                text << std::fixed << std::setprecision(0) << options.forecastFrames / fps * 1000
                     << " ms forecast | residual " << error << " mm | yellow: past prediction for this frame";
                label(annotated, text.str(), {9, 44}, 0.46, {0, 235, 255});
            }
        }
        if (snapshot.empty() || (!snapshotHasPose && !observations.empty())) {
            snapshot = annotated;
            snapshotHasPose = !observations.empty();
            snapshotFrame = frameNo;
        }
        if (writer.isOpened()) {
            writer.write(annotated);
        }
        ++frames;
        if (!options.headless) {
            const int delay = video ? std::clamp(cvRound(1000 / fps), 1, 250) : 0;
            if (!preview(annotated, delay, video)) {
                break;
            }
        }
    } while (video && (options.maxFrames == 0 || frames < options.maxFrames) && input.read(frame));

    writer.release();
    saveImage(options.output / "annotated.png", snapshot);
    csv.close();
    std::ostringstream summary;
    summary << "frames=" << frames << " frames_with_plates=" << framesWithPlates << " detections=" << detections
            << " numbered=" << numbered << " poses=" << poses << " snapshot_frame=" << snapshotFrame << '\n';
    if (options.forecastFrames) {
        forecastLog.close();
        summary << "forecast_horizon_s=" << options.forecastFrames / fps << " comparisons=" << comparisons;
        if (comparisons) {
            summary << " mean_forecast_error_mm=" << predictionError / comparisons
                    << " mean_hold_error_mm=" << holdError / comparisons;
        }
        summary << '\n';
    }
    report << summary.str();
    report.close();
    std::cout << summary.str() << "Results: " << fs::absolute(options.output).u8string() << '\n';
}

int run(const std::vector<std::string>& arguments) {
    try {
        if (arguments.empty()) {
            usage();
            return 0;
        }
        const auto options = parse(arguments);
        if (options.help) {
            usage();
        } else if (options.verify) {
            verifyPoseMath();
        } else if (!options.training.empty()) {
            checkOutput(options.model, {options.training});
            StickerModel::train(options.training, options.model);
        } else if (!options.calibration.empty()) {
            if (options.board.width < 3 || options.board.height < 3 || options.squareMm <= 0 ||
                (options.output.extension() != ".yml" && options.output.extension() != ".yaml")) {
                throw std::runtime_error("Calibration requires --board COLSxROWS, --square-mm MM and --out CAMERA.yml");
            }
            calibrateCamera(options.calibration, options.board, options.squareMm, options.output);
        } else {
            runMedia(options);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        arguments.push_back(fs::path(argv[i]).u8string());
    }
    return run(arguments);
}
#else
int main(int argc, char* argv[]) {
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
