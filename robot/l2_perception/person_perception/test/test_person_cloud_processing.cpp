// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "person_perception/person_cloud_processing.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

namespace pp = person_perception;

static pp::FilterConfig unfiltered() {
  pp::FilterConfig config;
  config.mask_erosion_pixels = 0;
  config.max_local_depth_jump_m = 0;
  return config;
}

TEST(PersonCloud, SelectsPersonAndTransformsOpticalAxes) {
  cv::Mat points(1, 2, CV_32FC3, cv::Scalar(0.25, -0.5, 2));
  cv::Mat labels(1, 2, CV_8UC1, cv::Scalar(0));
  labels.at<uint8_t>(0, 0) = 1;
  auto result = pp::extractPersonPoints(points, labels, {}, unfiltered());
  ASSERT_EQ(result.size(), 1u);
  EXPECT_FLOAT_EQ(result[0].x, 2);
  EXPECT_FLOAT_EQ(result[0].y, -0.25);
  EXPECT_FLOAT_EQ(result[0].z, 0.5);
  EXPECT_EQ(result[0].r, 128);
  labels.setTo(0);
  EXPECT_TRUE(pp::extractPersonPoints(points, labels, {}, unfiltered()).empty());
}

TEST(PersonCloud, RejectsNonFiniteCoordinatesOnEveryAxis) {
  cv::Mat points(1, 6, CV_32FC3, cv::Scalar(0, 0, 1));
  for (int axis = 0; axis < 3; ++axis) {
    points.at<cv::Vec3f>(0, axis)[axis] = std::numeric_limits<float>::quiet_NaN();
    points.at<cv::Vec3f>(0, axis + 3)[axis] = std::numeric_limits<float>::infinity();
  }
  cv::Mat labels(1, 6, CV_8UC1, cv::Scalar(1));
  EXPECT_TRUE(pp::extractPersonPoints(points, labels, {}, unfiltered()).empty());
}

TEST(PersonCloud, DepthAndPlanarRangeAreIndependentInclusiveBounds) {
  auto config = unfiltered();
  config.min_depth_m = 0.5;
  config.max_depth_m = 2;
  cv::Mat points(1, 5, CV_32FC3);
  points.at<cv::Vec3f>(0, 0) = {0, 0, 0.5};
  points.at<cv::Vec3f>(0, 1) = {0, 0, 2};
  points.at<cv::Vec3f>(0, 2) = {0, 0, 0.49};
  points.at<cv::Vec3f>(0, 3) = {0, 0, 2.01};
  points.at<cv::Vec3f>(0, 4) = {3, 0, 1};
  cv::Mat labels(1, 5, CV_8UC1, cv::Scalar(1));
  EXPECT_EQ(pp::extractPersonPoints(points, labels, {}, config).size(), 2u);
}

TEST(PersonCloud, ErosionRemovesTwoPixelSemanticBoundary) {
  cv::Mat points(7, 7, CV_32FC3, cv::Scalar(0, 0, 1));
  cv::Mat labels(7, 7, CV_8UC1, cv::Scalar(0));
  labels(cv::Rect(1, 1, 5, 5)).setTo(1);
  auto config = unfiltered();
  EXPECT_EQ(pp::extractPersonPoints(points, labels, {}, config).size(), 25u);
  config.mask_erosion_pixels = 2;
  EXPECT_EQ(pp::extractPersonPoints(points, labels, {}, config).size(), 1u);
}

TEST(PersonCloud, LocalMinimumRejectsBackgroundBleedOnlyWithinPersonMask) {
  cv::Mat points(1, 3, CV_32FC3);
  points.at<cv::Vec3f>(0, 0) = {0, 0, 0.5};
  points.at<cv::Vec3f>(0, 1) = {0, 0, 1};
  points.at<cv::Vec3f>(0, 2) = {0, 0, 1.5};
  cv::Mat labels(1, 3, CV_8UC1, cv::Scalar(1));
  labels.at<uint8_t>(0, 0) = 0;
  auto config = unfiltered();
  config.max_local_depth_jump_m = 0.25;
  auto result = pp::extractPersonPoints(points, labels, {}, config);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_FLOAT_EQ(result[0].x, 1);
  config.max_local_depth_jump_m = 0;
  EXPECT_EQ(pp::extractPersonPoints(points, labels, {}, config).size(), 2u);
}

TEST(PersonCloud, InvalidVerticalCoordinateCannotContaminateLocalMinimum) {
  cv::Mat points(1, 2, CV_32FC3, cv::Scalar(0, 0, 1));
  points.at<cv::Vec3f>(0, 0) = {0, std::numeric_limits<float>::quiet_NaN(), 0.3};
  cv::Mat labels(1, 2, CV_8UC1, cv::Scalar(1));
  auto config = unfiltered();
  config.max_local_depth_jump_m = 0.25;
  EXPECT_EQ(pp::extractPersonPoints(points, labels, {}, config).size(), 1u);
}

TEST(PersonCloud, AppliesStrideAndMinimumCount) {
  cv::Mat points(4, 4, CV_32FC3, cv::Scalar(0, 0, 1));
  cv::Mat labels(4, 4, CV_8UC1, cv::Scalar(1));
  auto config = unfiltered();
  config.pixel_stride = 2;
  EXPECT_EQ(pp::extractPersonPoints(points, labels, {}, config).size(), 4u);
  config.min_person_points = 5;
  EXPECT_TRUE(pp::extractPersonPoints(points, labels, {}, config).empty());
}

TEST(PersonCloud, PreservesRgbTextureChannels) {
  cv::Mat rgb(1, 1, CV_8UC3, cv::Scalar(210, 120, 30));
  const auto bgr = pp::toBgr(rgb, "rgb8");
  cv::Mat points(1, 1, CV_32FC3, cv::Scalar(0, 0, 1));
  cv::Mat labels(1, 1, CV_8UC1, cv::Scalar(1));
  auto result = pp::extractPersonPoints(points, labels, bgr, unfiltered());
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].r, 210);
  EXPECT_EQ(result[0].g, 120);
  EXPECT_EQ(result[0].b, 30);
}

TEST(PersonCloud, SupportsMonoAndAlphaTextures) {
  EXPECT_EQ(pp::toBgr(cv::Mat(1, 1, CV_8UC1, cv::Scalar(42)), "mono8")
                .at<cv::Vec3b>(0, 0), cv::Vec3b(42, 42, 42));
  EXPECT_EQ(pp::toBgr(cv::Mat(1, 1, CV_8UC4, cv::Scalar(3, 2, 1, 255)), "rgba8")
                .at<cv::Vec3b>(0, 0), cv::Vec3b(1, 2, 3));
  EXPECT_EQ(pp::toBgr(cv::Mat(1, 1, CV_8UC4, cv::Scalar(3, 2, 1, 255)), "bgra8")
                .at<cv::Vec3b>(0, 0), cv::Vec3b(3, 2, 1));
  EXPECT_THROW(pp::toBgr(cv::Mat(1, 1, CV_8UC1), "rgb8"), std::invalid_argument);
}

TEST(PersonCloud, RejectsShapeAndTypeMismatch) {
  cv::Mat points(1, 1, CV_32FC3, cv::Scalar(0, 0, 1));
  cv::Mat labels(1, 1, CV_8UC1, cv::Scalar(1));
  EXPECT_THROW(pp::extractPersonPoints({}, labels, {}, unfiltered()), std::invalid_argument);
  EXPECT_THROW(pp::extractPersonPoints(points, cv::Mat(2, 1, CV_8UC1), {}, unfiltered()),
               std::invalid_argument);
  EXPECT_THROW(pp::extractPersonPoints(points, labels, cv::Mat(1, 1, CV_8UC1), unfiltered()),
               std::invalid_argument);
}

TEST(PersonCloud, RejectsInvalidFilterConfiguration) {
  const auto good = unfiltered();
  auto bad = good;
  bad.min_depth_m = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(pp::validateConfig(bad), std::invalid_argument);
  bad = good; bad.max_planar_range_m = std::numeric_limits<double>::infinity();
  EXPECT_THROW(pp::validateConfig(bad), std::invalid_argument);
  bad = good; bad.mask_erosion_pixels = 4;
  EXPECT_THROW(pp::validateConfig(bad), std::invalid_argument);
  bad = good; bad.person_label_id = 256;
  EXPECT_THROW(pp::validateConfig(bad), std::invalid_argument);
  bad = good; bad.pixel_stride = 0;
  EXPECT_THROW(pp::validateConfig(bad), std::invalid_argument);
  bad = good; bad.min_person_points = 0;
  EXPECT_THROW(pp::validateConfig(bad), std::invalid_argument);
  bad = good; bad.max_depth_m = bad.min_depth_m;
  EXPECT_THROW(pp::validateConfig(bad), std::invalid_argument);
  bad = good; bad.max_local_depth_jump_m = -1;
  EXPECT_THROW(pp::validateConfig(bad), std::invalid_argument);
}
