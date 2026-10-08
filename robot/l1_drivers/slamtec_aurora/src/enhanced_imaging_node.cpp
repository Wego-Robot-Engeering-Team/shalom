// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <opencv2/core.hpp>
#include <aurora_pubsdk_inc.h>
#include <cxx/slamtec_remote_public.hxx>

#include <interfaces/msg/semantic_depth_frame.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace aurora = rp::standalone::aurora;
using Frame = interfaces::msg::SemanticDepthFrame;
using namespace std::chrono_literals;

class EnhancedImagingNode : public rclcpp::Node {
public:
  EnhancedImagingNode() : Node("aurora_imaging") {
    ip_ = declare_parameter<std::string>("ip_address", "192.168.11.1");
    rate_ = declare_parameter<double>("max_publish_rate_hz", 10.0);
    reconnect_ms_ = declare_parameter<int>("reconnect_wait_ms", 3000);
    camera_frame_ = declare_parameter<std::string>("camera_frame_id", "aurora_camera_left");
    depth_frame_ = declare_parameter<std::string>("depth_frame_id", "aurora_depth_optical");
    const auto topic = declare_parameter<std::string>("frame_topic", "/aurora/imaging/frame");
    if (ip_.empty() || !std::isfinite(rate_) || rate_ <= 0.0 || reconnect_ms_ < 0 ||
        camera_frame_.empty() || depth_frame_.empty() || topic.empty()) {
      throw std::invalid_argument("Invalid aurora_imaging parameters");
    }
    // Publish one bundle, so labels, depth and texture cannot be paired across
    // different ROS deliveries. Keep only the latest frame under backpressure.
    publisher_ = create_publisher<Frame>(topic, rclcpp::SensorDataQoS().keep_last(1));
  }

  void run() {
    while (rclcpp::ok()) {
      auto sdk = std::unique_ptr<aurora::RemoteSDK, decltype(&aurora::RemoteSDK::DestroySession)>(
          aurora::RemoteSDK::CreateSession(), &aurora::RemoteSDK::DestroySession);
      slamtec_aurora_sdk_errorcode_t error = SLAMTEC_AURORA_SDK_ERRORCODE_OK;
      if (!sdk || !sdk->connect(aurora::SDKServerConnectionDesc(ip_.c_str()), &error)) {
        RCLCPP_WARN(get_logger(), "Aurora %s connection failed (SDK error %d)",
                    ip_.c_str(), static_cast<int>(error));
        publishEmpty();
        retryPause();
        continue;
      }
      RCLCPP_INFO(get_logger(), "Connected to Aurora %s", ip_.c_str());
      if (!sdk->enhancedImaging.isDepthCameraReady() ||
          !sdk->enhancedImaging.isSemanticSegmentationReady() ||
          !sdk->setEnhancedImagingSubscription(SLAMTEC_AURORA_SDK_ENHANCED_IMAGE_TYPE_SEMANTIC, true) ||
          !sdk->setEnhancedImagingSubscription(SLAMTEC_AURORA_SDK_ENHANCED_IMAGE_TYPE_DEPTH, true)) {
        RCLCPP_ERROR(get_logger(), "Aurora semantic/depth stream unavailable");
        publishEmpty();
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
        if (semantic.desc.timestamp_ns != 0 && semantic.desc.timestamp_ns == previous_semantic_ns) {
          continue;
        }
        previous_semantic_ns = semantic.desc.timestamp_ns;
        const auto tick = std::chrono::steady_clock::now();
        if (previous_publish != std::chrono::steady_clock::time_point{} &&
            std::chrono::duration<double>(tick - previous_publish).count() < 1.0 / rate_) {
          continue;
        }
        previous_publish = tick;
        publishFrame(*sdk, semantic);
      }
      publishEmpty();
      sdk->disconnect();
      if (rclcpp::ok()) {
        RCLCPP_WARN(get_logger(), "Aurora disconnected; retrying");
        retryPause();
      }
    }
  }

private:
  void retryPause() const {
    // Use subtraction, avoiding overflow for a large configured delay.
    for (int remaining = reconnect_ms_; remaining > 0 && rclcpp::ok();) {
      const int pause = remaining < 100 ? remaining : 100;
      std::this_thread::sleep_for(std::chrono::milliseconds(pause));
      remaining -= pause;
    }
  }

  sensor_msgs::msg::Image image(const cv::Mat &matrix, const std::string &encoding,
                               const std::string &frame, const rclcpp::Time &stamp) const {
    sensor_msgs::msg::Image result;
    if (matrix.empty()) {
      return result;
    }
    result.header.stamp = stamp;
    result.header.frame_id = frame;
    result.width = matrix.cols;
    result.height = matrix.rows;
    result.encoding = encoding;
    const uint16_t endian_test = 1;
    result.is_bigendian = *reinterpret_cast<const uint8_t *>(&endian_test) == 0;
    result.step = matrix.cols * matrix.elemSize();
    result.data.resize(static_cast<size_t>(result.step) * result.height);
    for (int row = 0; row < matrix.rows; ++row) {
      std::memcpy(result.data.data() + static_cast<size_t>(row) * result.step,
                  matrix.ptr(row), result.step);
    }
    return result;
  }

  std::string colorEncoding(const cv::Mat &matrix, uint32_t format) const {
    if (format == 0 && matrix.type() == CV_8UC1) return "mono8";
    if (format == 1 && matrix.type() == CV_8UC3) return "rgb8";
    if (format == 2 && matrix.type() == CV_8UC4) return "rgba8";
    return {};
  }

  void publishEmpty() {
    Frame frame;
    frame.header.stamp = now();
    frame.header.frame_id = depth_frame_;
    publisher_->publish(frame);
  }

  void publishFrame(aurora::RemoteSDK &sdk, const aurora::RemoteEnhancedImagingFrame &semantic) {
    Frame frame;
    const auto stamp = now();
    frame.header.stamp = stamp;
    frame.header.frame_id = depth_frame_;
    frame.semantic_timestamp_ns = semantic.desc.timestamp_ns;
    cv::Mat labels;
    if (semantic.image.toMat(labels) && labels.type() == CV_8UC1) {
      frame.semantic = image(labels, "mono8", camera_frame_, stamp);
    }

    slamtec_aurora_sdk_errorcode_t error = SLAMTEC_AURORA_SDK_ERRORCODE_OK;
    aurora::RemoteStereoImagePair cameras;
    cv::Mat camera;
    if (sdk.dataProvider.peekCameraPreviewImage(cameras, semantic.desc.timestamp_ns, true, &error) &&
        cameras.leftImage.toMat(camera)) {
      const auto encoding = colorEncoding(camera, cameras.desc.left_image_desc.format);
      if (!encoding.empty()) frame.camera = image(camera, encoding, camera_frame_, stamp);
    }

    aurora::RemoteEnhancedImagingFrame depth, aligned, texture;
    cv::Mat points, aligned_labels, texture_pixels;
    if (sdk.enhancedImaging.peekDepthCameraFrame(
            depth, SLAMTEC_AURORA_SDK_DEPTHCAM_FRAME_TYPE_POINT3D, &error) &&
        depth.image.toMat(points) && points.type() == CV_32FC3) {
      frame.depth_timestamp_ns = depth.desc.timestamp_ns;
      frame.depth_points = image(points, "32FC3", depth_frame_, stamp);
      // Calibration/pixel registration belongs to the sensor adapter; L2
      // receives class IDs and selects the person class itself.
      if (sdk.enhancedImaging.calcDepthCameraAlignedSegmentationMap(semantic.image, aligned, &error) &&
          aligned.image.toMat(aligned_labels) && aligned_labels.type() == CV_8UC1 &&
          aligned_labels.size() == points.size()) {
        frame.depth_labels = image(aligned_labels, "mono8", depth_frame_, stamp);
      }
      if (sdk.enhancedImaging.peekDepthCameraRelatedRectifiedImage(
              texture, depth.desc.timestamp_ns, &error) && texture.image.toMat(texture_pixels) &&
          texture_pixels.size() == points.size()) {
        const auto encoding = colorEncoding(texture_pixels, texture.desc.image_desc.format);
        if (!encoding.empty()) frame.texture = image(texture_pixels, encoding, depth_frame_, stamp);
      }
    } else {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                          "Cannot read depth points (SDK error %d)", static_cast<int>(error));
    }
    publisher_->publish(std::move(frame));
  }

  std::string ip_, camera_frame_, depth_frame_;
  double rate_;
  int reconnect_ms_;
  rclcpp::Publisher<Frame>::SharedPtr publisher_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  try {
    std::make_shared<EnhancedImagingNode>()->run();
  } catch (const std::exception &error) {
    std::fprintf(stderr, "aurora_imaging: %s\n", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
