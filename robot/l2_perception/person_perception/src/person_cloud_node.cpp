// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "person_perception/person_cloud_processing.hpp"
#include <interfaces/msg/semantic_depth_frame.hpp>
#include <opencv2/imgproc.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <sensor_msgs/point_cloud2_iterator.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Frame = interfaces::msg::SemanticDepthFrame;
using person_perception::ColoredPoint;
using namespace std::chrono_literals;

class PersonCloudNode : public rclcpp::Node {
public:
  PersonCloudNode() : Node("aurora_person_cloud") {
    filter_.person_label_id = declare_parameter<int>("person_label_id", 1);
    filter_.min_depth_m = declare_parameter<double>("min_depth_m", 0.2);
    filter_.max_depth_m = declare_parameter<double>("max_depth_m", 3.0);
    filter_.max_planar_range_m = declare_parameter<double>("max_planar_range_m", 3.0);
    filter_.mask_erosion_pixels = declare_parameter<int>("mask_erosion_pixels", 2);
    filter_.max_local_depth_jump_m = declare_parameter<double>("max_local_depth_jump_m", 0.25);
    filter_.pixel_stride = declare_parameter<int>("pixel_stride", 1);
    filter_.min_person_points = declare_parameter<int>("min_person_points", 1);
    sync_tolerance_ms_ = declare_parameter<int>("sync_tolerance_ms", 100);
    input_timeout_ms_ = declare_parameter<int>("input_timeout_ms", 1000);
    frame_id_ = declare_parameter<std::string>("frame_id", "aurora_depth_local");
    const auto input = declare_parameter<std::string>("input_topic", "/aurora/imaging/frame");
    const auto topic3d = declare_parameter<std::string>("points3d_topic", "/aurora/person/points3d");
    const auto topic2d = declare_parameter<std::string>("points2d_topic", "/aurora/person/points2d");
    const auto camera_topic = declare_parameter<std::string>("camera_image_topic", "/aurora/person/camera_image");
    const auto overlay_topic = declare_parameter<std::string>("overlay_image_topic", "/aurora/person/overlay_image");
    person_perception::validateConfig(filter_);
    if (sync_tolerance_ms_ < 0 || input_timeout_ms_ <= 0 || frame_id_.empty() || input.empty() ||
        topic3d.empty() || topic2d.empty() || camera_topic.empty() || overlay_topic.empty()) {
      throw std::invalid_argument("Invalid aurora_person_cloud transport parameters");
    }
    cloud3d_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>(topic3d, rclcpp::SensorDataQoS());
    cloud2d_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>(topic2d, rclcpp::SensorDataQoS());
    camera_pub_ = create_publisher<sensor_msgs::msg::Image>(camera_topic, rclcpp::SensorDataQoS());
    overlay_pub_ = create_publisher<sensor_msgs::msg::Image>(overlay_topic, rclcpp::SensorDataQoS());
    subscription_ = create_subscription<Frame>(input, rclcpp::SensorDataQoS().keep_last(1),
        [this](Frame::ConstSharedPtr frame) { processFrame(*frame); });
    timeout_timer_ = create_wall_timer(100ms, [this]() {
      if (!stale_ && std::chrono::steady_clock::now() - last_arrival_ >
          std::chrono::milliseconds(input_timeout_ms_)) {
        stale_ = true;
        publishClouds({}, now());
        RCLCPP_WARN(get_logger(), "Aurora input timed out; cleared person clouds");
      }
    });
  }

private:
  cv::Mat readImage(const sensor_msgs::msg::Image &image) const {
    int type;
    if (image.encoding == "mono8") type = CV_8UC1;
    else if (image.encoding == "rgb8" || image.encoding == "bgr8") type = CV_8UC3;
    else if (image.encoding == "rgba8" || image.encoding == "bgra8") type = CV_8UC4;
    else if (image.encoding == "32FC3") type = CV_32FC3;
    else throw std::invalid_argument("Unsupported image encoding: " + image.encoding);
    const auto element_size = CV_ELEM_SIZE(type);
    if (!image.width || !image.height ||
        image.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        image.height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        image.step < static_cast<uint64_t>(image.width) * element_size ||
        image.data.size() < static_cast<uint64_t>(image.step) * image.height) {
      throw std::invalid_argument("Invalid image dimensions, stride or data length");
    }
    // Copy rows, including support for padded input and unaligned float bytes.
    cv::Mat matrix(image.height, image.width, type);
    const size_t row_size = static_cast<size_t>(image.width) * element_size;
    const uint16_t endian_test = 1;
    const bool host_bigendian = *reinterpret_cast<const uint8_t *>(&endian_test) == 0;
    for (uint32_t row = 0; row < image.height; ++row) {
      auto *target = matrix.ptr(row);
      std::memcpy(target, image.data.data() + static_cast<size_t>(row) * image.step, row_size);
      if (type == CV_32FC3 && static_cast<bool>(image.is_bigendian) != host_bigendian) {
        for (size_t offset = 0; offset < row_size; offset += sizeof(float)) {
          std::reverse(target + offset, target + offset + sizeof(float));
        }
      }
    }
    return matrix;
  }

  void publishCamera(const Frame &frame) {
    if (frame.camera.data.empty()) return;
    try {
      const auto camera = readImage(frame.camera);
      auto background = person_perception::toBgr(camera, frame.camera.encoding);
      camera_pub_->publish(frame.camera);
      if (frame.semantic.encoding != "mono8") return;
      const auto labels = readImage(frame.semantic);
      if (labels.size() != camera.size()) return;
      const cv::Mat mask = labels == filter_.person_label_id;
      cv::Mat overlay = background.clone(), blended;
      cv::addWeighted(background, 0.5, cv::Mat(background.size(), CV_8UC3, cv::Scalar(0, 255, 0)),
                      0.5, 0.0, blended);
      blended.copyTo(overlay, mask);
      sensor_msgs::msg::Image message;
      message.header = frame.camera.header;
      message.encoding = "bgr8";
      message.height = overlay.rows;
      message.width = overlay.cols;
      message.step = overlay.cols * 3;
      message.data.assign(overlay.data, overlay.data + static_cast<size_t>(message.step) * message.height);
      overlay_pub_->publish(message);
    } catch (const std::exception &error) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                          "Camera/semantic image rejected: %s", error.what());
    }
  }

  void processFrame(const Frame &frame) {
    last_arrival_ = std::chrono::steady_clock::now();
    stale_ = false;
    publishCamera(frame);
    try {
      const auto a = frame.semantic_timestamp_ns, b = frame.depth_timestamp_ns;
      const auto difference = a > b ? a - b : b - a;
      if (a && b && difference > static_cast<uint64_t>(sync_tolerance_ms_) * 1000000ULL) {
        throw std::invalid_argument("Semantic/depth device timestamp offset exceeds tolerance");
      }
      if (frame.depth_points.encoding != "32FC3" || frame.depth_labels.encoding != "mono8") {
        throw std::invalid_argument("Expected 32FC3 optical XYZ and mono8 registered labels");
      }
      const auto points = readImage(frame.depth_points);
      const auto labels = readImage(frame.depth_labels);
      cv::Mat texture;
      if (!frame.texture.data.empty()) {
        try {
          auto candidate = person_perception::toBgr(readImage(frame.texture), frame.texture.encoding);
          if (candidate.size() == points.size()) texture = candidate;
        } catch (const std::exception &) {
          // Depth remains usable when its optional color texture is invalid.
        }
      }
      const auto result = person_perception::extractPersonPoints(points, labels, texture, filter_);
      RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 5000, "Person points: %zu", result.size());
      publishClouds(result, frame.header.stamp);
    } catch (const std::exception &error) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "Person frame rejected: %s", error.what());
      publishClouds({}, frame.header.stamp);
    }
  }

  void publishClouds(const std::vector<ColoredPoint> &points, const builtin_interfaces::msg::Time &stamp) {
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
        ++x; ++y; ++z; ++r; ++g; ++b;
      }
      return cloud;
    };
    cloud3d_pub_->publish(make_cloud(false));
    cloud2d_pub_->publish(make_cloud(true));
  }

  person_perception::FilterConfig filter_;
  int sync_tolerance_ms_, input_timeout_ms_;
  std::string frame_id_;
  bool stale_ = true;
  std::chrono::steady_clock::time_point last_arrival_;
  rclcpp::Subscription<Frame>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timeout_timer_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr cloud3d_pub_, cloud2d_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr camera_pub_, overlay_pub_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<PersonCloudNode>());
  } catch (const std::exception &error) {
    std::fprintf(stderr, "aurora_person_cloud: %s\n", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
