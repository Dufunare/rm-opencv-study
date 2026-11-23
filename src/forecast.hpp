#pragma once

#include "pose.hpp"

#include <cmath>
#include <deque>
#include <map>

namespace study {

struct Observation {
    int frame;
    double time;
    cv::Point2f pixel;
    std::string color;
    std::string size;
    float height;
    cv::Vec3d position;
};

struct Forecast {
    int originFrame;
    int evaluationFrame;
    double seconds;
    cv::Vec3d velocity;
    cv::Vec3d position;
    cv::Vec3d originPosition;
};

class MotionStudy {
public:
    MotionStudy(int horizon, double fps) : horizon_(horizon), fps_(fps) {}

    std::optional<Observation> associate(const std::vector<Observation>& candidates) const {
        if (candidates.empty()) {
            return std::nullopt;
        }
        if (history_.empty()) {
            return candidates.front();
        }
        const auto& last = history_.back();
        double bestDistance = std::max(12.0, 1.8 * last.height);
        std::optional<Observation> best;
        for (const auto& candidate : candidates) {
            const double distance = cv::norm(candidate.pixel - last.pixel);
            if (candidate.color == last.color && candidate.size == last.size && distance < bestDistance) {
                bestDistance = distance;
                best = candidate;
            }
        }
        return best;
    }

    std::optional<Forecast> observe(const std::optional<Observation>& observation) {
        if (!observation) {
            history_.clear();
            pending_.clear();
            return std::nullopt;
        }
        const auto& current = *observation;
        std::optional<Forecast> matured;
        const auto pending = pending_.find(current.frame);
        if (pending != pending_.end()) {
            matured = pending->second;
            pending_.erase(pending);
        }
        history_.push_back(current);
        if (history_.size() > 8) {
            history_.pop_front();
        }
        if (history_.size() >= 5) {
            double meanTime = 0;
            cv::Vec3d meanPosition(0, 0, 0);
            for (const auto& sample : history_) {
                meanTime += sample.time;
                meanPosition += sample.position;
            }
            const double n = static_cast<double>(history_.size());
            meanTime /= n;
            meanPosition /= n;
            double variance = 0;
            cv::Vec3d covariance(0, 0, 0);
            for (const auto& sample : history_) {
                const double dt = sample.time - meanTime;
                variance += dt * dt;
                covariance += dt * (sample.position - meanPosition);
            }
            const cv::Vec3d velocity = covariance / variance;
            const double seconds = horizon_ / fps_;
            const int evaluationFrame = current.frame + horizon_;
            pending_.emplace(evaluationFrame, Forecast{current.frame, evaluationFrame, seconds,
                velocity, current.position + seconds * velocity, current.position});
        }
        return matured;
    }

private:
    int horizon_;
    double fps_;
    std::deque<Observation> history_;
    std::map<int, Forecast> pending_;
};

} // namespace study
