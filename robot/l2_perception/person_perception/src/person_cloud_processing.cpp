// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "person_perception/person_cloud_processing.hpp"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace person_perception {

void validateConfig(const FilterConfig &config) {
  if (config.person_label_id < 0 || config.person_label_id > 255 ||
      !std::isfinite(config.min_depth_m) || !std::isfinite(config.max_depth_m) ||
      !std::isfinite(config.max_planar_range_m) || !std::isfinite(config.max_local_depth_jump_m) ||
      config.min_depth_m < 0.0 || config.max_depth_m <= config.min_depth_m ||
      config.max_planar_range_m <= 0.0 || config.max_local_depth_jump_m < 0.0 ||
      config.mask_erosion_pixels < 0 || config.mask_erosion_pixels > 3 ||
      config.pixel_stride < 1 || config.min_person_points < 1) {
    throw std::invalid_argument("Invalid person cloud filter parameters");
  }
}

cv::Mat toBgr(const cv::Mat &image, const std::string &encoding) {
  cv::Mat result;
  if (encoding == "mono8" && image.type() == CV_8UC1) {
    cv::cvtColor(image, result, cv::COLOR_GRAY2BGR);
  } else if (encoding == "rgb8" && image.type() == CV_8UC3) {
    cv::cvtColor(image, result, cv::COLOR_RGB2BGR);
  } else if (encoding == "rgba8" && image.type() == CV_8UC4) {
    cv::cvtColor(image, result, cv::COLOR_RGBA2BGR);
  } else if (encoding == "bgra8" && image.type() == CV_8UC4) {
    cv::cvtColor(image, result, cv::COLOR_BGRA2BGR);
  } else if (encoding == "bgr8" && image.type() == CV_8UC3) {
    result = image;
  } else {
    throw std::invalid_argument("Unsupported camera encoding or pixel type");
  }
  return result;
}

std::vector<ColoredPoint> extractPersonPoints(
    const cv::Mat &points, const cv::Mat &labels, const cv::Mat &texture,
    const FilterConfig &config) {
  validateConfig(config);
  if (points.empty() || points.type() != CV_32FC3 || labels.type() != CV_8UC1 ||
      points.size() != labels.size()) {
    throw std::invalid_argument("Person labels and optical XYZ dimensions/types must match");
  }
  if (!texture.empty() && (texture.type() != CV_8UC3 || texture.size() != points.size())) {
    throw std::invalid_argument("Texture must be depth-registered BGR8");
  }

  cv::Mat mask = labels == config.person_label_id;
  if (config.mask_erosion_pixels > 0) {
    cv::erode(mask, mask, cv::Mat::ones(3, 3, CV_8U), cv::Point(-1, -1),
              config.mask_erosion_pixels);
  }
  cv::Mat local_min;
  if (config.max_local_depth_jump_m > 0.0) {
    cv::Mat person_depth(points.size(), CV_32FC1,
                         cv::Scalar(std::numeric_limits<float>::infinity()));
    for (int row = 0; row < points.rows; ++row) {
      const auto *mask_row = mask.ptr<uint8_t>(row);
      const auto *point_row = points.ptr<cv::Vec3f>(row);
      auto *depth_row = person_depth.ptr<float>(row);
      for (int col = 0; col < points.cols; ++col) {
        const auto &p = point_row[col];
        if (mask_row[col] != 0 && std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]) &&
            p[2] >= config.min_depth_m && p[2] <= config.max_depth_m &&
            std::hypot(p[0], p[2]) <= config.max_planar_range_m) {
          depth_row[col] = p[2];
        }
      }
    }
    cv::erode(person_depth, local_min, cv::Mat::ones(5, 5, CV_8U));
  }

  std::vector<ColoredPoint> result;
  for (int row = 0; row < points.rows; row += config.pixel_stride) {
    const auto *mask_row = mask.ptr<uint8_t>(row);
    const auto *point_row = points.ptr<cv::Vec3f>(row);
    const auto *local_row = local_min.empty() ? nullptr : local_min.ptr<float>(row);
    for (int col = 0; col < points.cols; col += config.pixel_stride) {
      const auto &p = point_row[col];
      if (mask_row[col] == 0 || !std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) ||
          p[2] < config.min_depth_m || p[2] > config.max_depth_m ||
          std::hypot(p[0], p[2]) > config.max_planar_range_m ||
          (local_row && p[2] > local_row[col] + config.max_local_depth_jump_m)) {
        continue;
      }
      // Optical right/down/forward -> camera-local ROS forward/left/up.
      ColoredPoint point{p[2], -p[0], -p[1], 128, 128, 128};
      if (!texture.empty()) {
        const auto color = texture.at<cv::Vec3b>(row, col);
        point.r = color[2];
        point.g = color[1];
        point.b = color[0];
      }
      result.push_back(point);
    }
  }
  if (result.size() < static_cast<size_t>(config.min_person_points)) result.clear();
  return result;
}

}  // namespace person_perception
