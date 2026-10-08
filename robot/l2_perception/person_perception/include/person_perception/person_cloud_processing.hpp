// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary
#pragma once

#include <opencv2/core.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace person_perception {

struct FilterConfig {
  int person_label_id = 1;
  double min_depth_m = 0.2;
  double max_depth_m = 3.0;
  double max_planar_range_m = 3.0;
  int mask_erosion_pixels = 2;
  double max_local_depth_jump_m = 0.25;
  int pixel_stride = 1;
  int min_person_points = 1;
};

struct ColoredPoint {
  float x, y, z;
  uint8_t r, g, b;
};

void validateConfig(const FilterConfig &config);
// Normalize explicitly encoded camera pixels to OpenCV BGR.
cv::Mat toBgr(const cv::Mat &image, const std::string &encoding);
// points: organized optical XYZ (metres); labels: depth-registered class IDs.
std::vector<ColoredPoint> extractPersonPoints(
    const cv::Mat &points, const cv::Mat &labels, const cv::Mat &texture_bgr,
    const FilterConfig &config);

}  // namespace person_perception
