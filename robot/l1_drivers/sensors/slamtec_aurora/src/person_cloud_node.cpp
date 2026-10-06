// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <aurora_pubsdk_inc.h>
#include <cxx/slamtec_remote_public.hxx>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/point_cloud2_iterator.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace aurora = rp::standalone::aurora;
using namespace std::chrono_literals;

struct ColoredPoint {
  float x, y, z;
  uint8_t r, g, b;
};

class PersonCloudNode : public rclcpp::Node {
public:
  PersonCloudNode() : Node("aurora_person_cloud") {
    ip_address_ = declare_parameter<std::string>("ip_address", "192.168.11.1");
    person_label_id_ = declare_parameter<int>("person_label_id", 1);
    min_depth_m_ = declare_parameter<double>("min_depth_m", 0.2);
    max_depth_m_ = declare_parameter<double>("max_depth_m", 3.0);
    max_planar_range_m_ = declare_parameter<double>("max_planar_range_m", 3.0);
    mask_erosion_pixels_ = declare_parameter<int>("mask_erosion_pixels", 2);
    max_local_depth_jump_m_ = declare_parameter<double>("max_local_depth_jump_m", 0.25);
    pixel_stride_ = declare_parameter<int>("pixel_stride", 1);
    min_person_points_ = declare_parameter<int>("min_person_points", 1);
    sync_tolerance_ms_ = declare_parameter<int>("sync_tolerance_ms", 100);
    max_publish_rate_hz_ = declare_parameter<double>("max_publish_rate_hz", 10.0);
    reconnect_wait_ms_ = declare_parameter<int>("reconnect_wait_ms", 3000);
    frame_id_ = declare_parameter<std::string>("frame_id", "aurora_depth_local");
    const auto topic3d = declare_parameter<std::string>("points3d_topic", "/aurora/person/points3d");
    const auto topic2d = declare_parameter<std::string>("points2d_topic", "/aurora/person/points2d");
    const auto camera_topic = declare_parameter<std::string>("camera_image_topic", "/aurora/person/camera_image");
    const auto overlay_topic = declare_parameter<std::string>("overlay_image_topic", "/aurora/person/overlay_image");

    if (person_label_id_ < 0 || person_label_id_ > 255 || min_depth_m_ < 0.0 ||
        max_depth_m_ <= min_depth_m_ || max_planar_range_m_ <= 0.0 ||
        max_local_depth_jump_m_ < 0.0 ||
        mask_erosion_pixels_ < 0 || mask_erosion_pixels_ > 3 ||
        pixel_stride_ < 1 || min_person_points_ < 1 ||
        sync_tolerance_ms_ < 0 || max_publish_rate_hz_ <= 0.0 || reconnect_wait_ms_ < 0 ||
        frame_id_.empty()) {
      throw std::invalid_argument("Invalid aurora_person_cloud parameters");
    }
    cloud3d_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>(topic3d, rclcpp::SensorDataQoS());
    cloud2d_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>(topic2d, rclcpp::SensorDataQoS());
    camera_pub_ = create_publisher<sensor_msgs::msg::Image>(camera_topic, rclcpp::SensorDataQoS());
    overlay_pub_ = create_publisher<sensor_msgs::msg::Image>(overlay_topic, rclcpp::SensorDataQoS());
  }

  void run() {
    while (rclcpp::ok()) {
      auto sdk = std::unique_ptr<aurora::RemoteSDK, decltype(&aurora::RemoteSDK::DestroySession)>(
          aurora::RemoteSDK::CreateSession(), &aurora::RemoteSDK::DestroySession);
      if (!sdk) {
        RCLCPP_ERROR(get_logger(), "Aurora SDK session creation failed");
        retryPause();
        continue;
      }

      slamtec_aurora_sdk_errorcode_t error = SLAMTEC_AURORA_SDK_ERRORCODE_OK;
      if (!sdk->connect(aurora::SDKServerConnectionDesc(ip_address_.c_str()), &error)) {
        RCLCPP_WARN(get_logger(), "Aurora %s connection failed (SDK error %d)",
                    ip_address_.c_str(), static_cast<int>(error));
        retryPause();
        continue;
      }
      RCLCPP_INFO(get_logger(), "Connected to Aurora %s", ip_address_.c_str());

      if (!sdk->enhancedImaging.isDepthCameraReady() ||
          !sdk->enhancedImaging.isSemanticSegmentationReady()) {
        RCLCPP_ERROR(get_logger(), "Aurora depth camera or semantic segmentation is unavailable");
        sdk->disconnect();
        retryPause();
        continue;
      }
      slamtec_aurora_sdk_semantic_segmentation_label_info_t labels{};
      if (sdk->enhancedImaging.getSemanticSegmentationLabels(labels) &&
          static_cast<size_t>(person_label_id_) < labels.label_count) {
        RCLCPP_INFO(get_logger(), "Class %d: %s", person_label_id_,
                    labels.label_names[person_label_id_].name);
      }
      if (!sdk->setEnhancedImagingSubscription(SLAMTEC_AURORA_SDK_ENHANCED_IMAGE_TYPE_SEMANTIC, true) ||
          !sdk->setEnhancedImagingSubscription(SLAMTEC_AURORA_SDK_ENHANCED_IMAGE_TYPE_DEPTH, true)) {
        RCLCPP_ERROR(get_logger(), "Aurora enhanced imaging subscription failed");
        sdk->disconnect();
        retryPause();
        continue;
      }

      uint64_t previous_semantic_ns = 0;
      auto previous_publish = std::chrono::steady_clock::time_point{};
      while (rclcpp::ok() && sdk->isConnected()) {
        if (!sdk->enhancedImaging.waitSemanticSegmentationNextFrame(300)) {
          continue;
        }
        aurora::RemoteEnhancedImagingFrame semantic;
        if (!sdk->enhancedImaging.peekSemanticSegmentationFrame(semantic, &error)) {
          RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                               "Cannot read semantic frame (SDK error %d)", static_cast<int>(error));
          continue;
        }
        if (semantic.desc.timestamp_ns == previous_semantic_ns && previous_semantic_ns != 0) {
          continue;
        }
        previous_semantic_ns = semantic.desc.timestamp_ns;
        const auto now = std::chrono::steady_clock::now();
        if (previous_publish != std::chrono::steady_clock::time_point{} &&
            std::chrono::duration<double>(now - previous_publish).count() < 1.0 / max_publish_rate_hz_) {
          continue;
        }
        previous_publish = now;
        const auto stamp = this->now();
        publishCameraImages(*sdk, semantic, stamp);
        processFrame(*sdk, semantic, stamp);
      }
      publishClouds({}, now());
      sdk->disconnect();
      if (rclcpp::ok()) {
        RCLCPP_WARN(get_logger(), "Aurora disconnected; retrying");
        retryPause();
      }
    }
  }

private:
  void retryPause() const {
    const int slices = (reconnect_wait_ms_ + 99) / 100;
    for (int i = 0; i < slices && rclcpp::ok(); ++i) {
      std::this_thread::sleep_for(100ms);
    }
  }

  void publishImage(const cv::Mat &image, const std::string &encoding,
                    const rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr &publisher,
                    const rclcpp::Time &stamp) {
    sensor_msgs::msg::Image message;
    message.header.stamp = stamp;
    message.header.frame_id = "aurora_camera_left";
    message.height = image.rows;
    message.width = image.cols;
    message.encoding = encoding;
    message.is_bigendian = false;
    message.step = image.cols * image.elemSize();
    message.data.resize(static_cast<size_t>(message.step) * message.height);
    for (int row = 0; row < image.rows; ++row) {
      std::memcpy(message.data.data() + static_cast<size_t>(row) * message.step,
                  image.ptr(row), message.step);
    }
    publisher->publish(message);
  }

  void publishCameraImages(aurora::RemoteSDK &sdk,
                           const aurora::RemoteEnhancedImagingFrame &semantic,
                           const rclcpp::Time &stamp) {
    aurora::RemoteStereoImagePair cameras;
    slamtec_aurora_sdk_errorcode_t error = SLAMTEC_AURORA_SDK_ERRORCODE_OK;
    if (!sdk.dataProvider.peekCameraPreviewImage(cameras, semantic.desc.timestamp_ns, true, &error)) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "Cannot read Aurora camera preview (SDK error %d)", static_cast<int>(error));
      return;
    }

    cv::Mat left;
    if (!cameras.leftImage.toMat(left) || left.empty()) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "Aurora left camera image is empty");
      return;
    }
    const auto format = cameras.desc.left_image_desc.format;
    if (!camera_logged_) {
      RCLCPP_INFO(get_logger(), "Left camera preview: %dx%d, SDK format %u",
                  left.cols, left.rows, format);
      camera_logged_ = true;
    }

    cv::Mat background;
    if (format == 0 && left.type() == CV_8UC1) {
      publishImage(left, "mono8", camera_pub_, stamp);
      cv::cvtColor(left, background, cv::COLOR_GRAY2BGR);
    } else if (format == 1 && left.type() == CV_8UC3) {
      publishImage(left, "rgb8", camera_pub_, stamp);
      cv::cvtColor(left, background, cv::COLOR_RGB2BGR);
    } else if (format == 2 && left.type() == CV_8UC4) {
      publishImage(left, "rgba8", camera_pub_, stamp);
      cv::cvtColor(left, background, cv::COLOR_RGBA2BGR);
    } else {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "Unsupported camera image format %u, OpenCV type %d", format, left.type());
      return;
    }

    cv::Mat labels;
    if (!semantic.image.toMat(labels) || labels.type() != CV_8UC1 || labels.size() != left.size()) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "Camera/semantic image size mismatch; publishing camera without overlay");
      return;
    }
    // Highlight only pixels assigned to the configured person class.
    cv::Mat mask = labels == person_label_id_;
    cv::Mat overlay = background.clone();
    cv::Mat green(background.size(), CV_8UC3, cv::Scalar(0, 255, 0));
    cv::Mat blended;
    cv::addWeighted(background, 0.5, green, 0.5, 0.0, blended);
    blended.copyTo(overlay, mask);
    publishImage(overlay, "bgr8", overlay_pub_, stamp);
  }

  void processFrame(aurora::RemoteSDK &sdk, const aurora::RemoteEnhancedImagingFrame &semantic,
                    const rclcpp::Time &stamp) {
    aurora::RemoteEnhancedImagingFrame depth;
    slamtec_aurora_sdk_errorcode_t error = SLAMTEC_AURORA_SDK_ERRORCODE_OK;
    // Pass a non-null error pointer: this SDK wrapper dereferences it on success.
    if (!sdk.enhancedImaging.peekDepthCameraFrame(
            depth, SLAMTEC_AURORA_SDK_DEPTHCAM_FRAME_TYPE_POINT3D, &error)) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "Cannot read depth points (SDK error %d)", static_cast<int>(error));
      publishClouds({}, stamp);
      return;
    }
    if (semantic.desc.timestamp_ns != 0 && depth.desc.timestamp_ns != 0) {
      const auto a = semantic.desc.timestamp_ns;
      const auto b = depth.desc.timestamp_ns;
      const auto difference_ns = a > b ? a - b : b - a;
      if (difference_ns > static_cast<uint64_t>(sync_tolerance_ms_) * 1000000ULL) {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                             "Semantic/depth frame offset %.0f ms exceeds %d ms",
                             difference_ns / 1e6, sync_tolerance_ms_);
        publishClouds({}, stamp);
        return;
      }
    }

    aurora::RemoteEnhancedImagingFrame aligned;
    if (!sdk.enhancedImaging.calcDepthCameraAlignedSegmentationMap(semantic.image, aligned, &error)) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "Depth/semantic alignment failed (SDK error %d)", static_cast<int>(error));
      publishClouds({}, stamp);
      return;
    }

    cv::Mat labels, points;
    if (!aligned.image.toMat(labels) || !depth.image.toMat(points) ||
        labels.type() != CV_8UC1 || points.type() != CV_32FC3 || labels.size() != points.size()) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "Unexpected aligned image format or dimensions: labels=%dx%d type=%d, depth=%dx%d type=%d",
                           labels.cols, labels.rows, labels.type(), points.cols, points.rows, points.type());
      publishClouds({}, stamp);
      return;
    }

    aurora::RemoteEnhancedImagingFrame texture_frame;
    cv::Mat texture;
    if (sdk.enhancedImaging.peekDepthCameraRelatedRectifiedImage(
            texture_frame, depth.desc.timestamp_ns, &error) &&
        texture_frame.image.toMat(texture) && texture.size() == points.size() &&
        (texture.type() == CV_8UC1 || texture.type() == CV_8UC3 || texture.type() == CV_8UC4)) {
      if (!texture_logged_) {
        RCLCPP_INFO(get_logger(), "Depth-aligned camera texture: %dx%d, SDK format %u",
                    texture.cols, texture.rows, texture_frame.desc.image_desc.format);
        texture_logged_ = true;
      }
    } else {
      texture.release();
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "Depth-aligned camera texture unavailable; points use gray fallback");
    }

    cv::Mat person_mask = labels == person_label_id_;
    const int raw_mask_pixels = cv::countNonZero(person_mask);
    if (mask_erosion_pixels_ > 0) {
      cv::erode(person_mask, person_mask, cv::Mat::ones(3, 3, CV_8U),
                cv::Point(-1, -1), mask_erosion_pixels_);
    }

    // A far depth at a person silhouette is often a background pixel. Compare
    // it with nearby valid person depths and reject only the farther point.
    cv::Mat local_min_depth;
    if (max_local_depth_jump_m_ > 0.0) {
      cv::Mat person_depth(points.size(), CV_32FC1,
                           cv::Scalar(std::numeric_limits<float>::infinity()));
      for (int row = 0; row < points.rows; ++row) {
        const auto *mask_row = person_mask.ptr<uint8_t>(row);
        const auto *point_row = points.ptr<cv::Vec3f>(row);
        auto *depth_row = person_depth.ptr<float>(row);
        for (int col = 0; col < points.cols; ++col) {
          const auto &p = point_row[col];
          if (mask_row[col] != 0 && std::isfinite(p[0]) && std::isfinite(p[2]) &&
              p[2] >= min_depth_m_ && p[2] <= max_depth_m_ &&
              std::hypot(p[0], p[2]) <= max_planar_range_m_) {
            depth_row[col] = p[2];
          }
        }
      }
      cv::erode(person_depth, local_min_depth, cv::Mat::ones(5, 5, CV_8U));
    }

    std::vector<ColoredPoint> person_points;
    size_t depth_outliers = 0;
    for (int row = 0; row < points.rows; row += pixel_stride_) {
      const auto *mask_row = person_mask.ptr<uint8_t>(row);
      const auto *point_row = points.ptr<cv::Vec3f>(row);
      const auto *local_min_row = local_min_depth.empty() ? nullptr : local_min_depth.ptr<float>(row);
      for (int col = 0; col < points.cols; col += pixel_stride_) {
        if (mask_row[col] == 0) {
          continue;
        }
        const auto &p = point_row[col];  // Optical: X right, Y down, Z forward.
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) ||
            p[2] < min_depth_m_ || p[2] > max_depth_m_ ||
            std::hypot(p[0], p[2]) > max_planar_range_m_) {
          continue;
        }
        if (local_min_row && p[2] > local_min_row[col] + max_local_depth_jump_m_) {
          ++depth_outliers;
          continue;
        }
        // Camera-local ROS axes: X forward, Y left, Z up. No gravity alignment.
        ColoredPoint point{p[2], -p[0], -p[1], 128, 128, 128};
        if (!texture.empty()) {
          if (texture.type() == CV_8UC1) {
            point.r = point.g = point.b = texture.at<uint8_t>(row, col);
          } else if (texture.type() == CV_8UC3) {
            const auto color = texture.at<cv::Vec3b>(row, col);
            point.r = color[2];
            point.g = color[1];
            point.b = color[0];
          } else {
            const auto color = texture.at<cv::Vec4b>(row, col);
            point.r = color[2];
            point.g = color[1];
            point.b = color[0];
          }
        }
        person_points.push_back(point);
      }
    }
    if (person_points.size() < static_cast<size_t>(min_person_points_)) {
      person_points.clear();
    }
    RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 5000,
                         "Person points: %zu (mask %d -> %d, depth outliers %zu, image %dx%d)",
                         person_points.size(), raw_mask_pixels, cv::countNonZero(person_mask),
                         depth_outliers, points.cols, points.rows);
    publishClouds(person_points, stamp);
  }

  void publishClouds(const std::vector<ColoredPoint> &points, const rclcpp::Time &stamp) {
    auto make_cloud = [&](bool projected) {
      sensor_msgs::msg::PointCloud2 cloud;
      cloud.header.stamp = stamp;
      cloud.header.frame_id = frame_id_;
      cloud.is_dense = true;
      sensor_msgs::PointCloud2Modifier modifier(cloud);
      modifier.setPointCloud2FieldsByString(2, "xyz", "rgb");
      modifier.resize(points.size());
      sensor_msgs::PointCloud2Iterator<float> x(cloud, "x"), y(cloud, "y"), z(cloud, "z");
      sensor_msgs::PointCloud2Iterator<uint8_t> r(cloud, "r"), g(cloud, "g"), b(cloud, "b");
      for (const auto &point : points) {
        *x = point.x;
        *y = point.y;
        *z = projected ? 0.0f : point.z;
        *r = projected ? 255 : point.r;
        *g = projected ? 0 : point.g;
        *b = projected ? 0 : point.b;
        ++x;
        ++y;
        ++z;
        ++r;
        ++g;
        ++b;
      }
      return cloud;
    };
    cloud3d_pub_->publish(make_cloud(false));
    cloud2d_pub_->publish(make_cloud(true));
  }

  std::string ip_address_, frame_id_;
  int person_label_id_, pixel_stride_, min_person_points_, sync_tolerance_ms_, reconnect_wait_ms_;
  int mask_erosion_pixels_;
  double min_depth_m_, max_depth_m_, max_planar_range_m_, max_local_depth_jump_m_;
  double max_publish_rate_hz_;
  bool camera_logged_ = false;
  bool texture_logged_ = false;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr cloud3d_pub_, cloud2d_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr camera_pub_, overlay_pub_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  try {
    std::make_shared<PersonCloudNode>()->run();
  } catch (const std::exception &error) {
    fprintf(stderr, "aurora_person_cloud: %s\n", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
