// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <visualization_msgs/msg/marker_array.hpp>

namespace {
constexpr double kRadiansToDegrees = 57.29577951308232;

geometry_msgs::msg::Point point(double x, double y, double z = 0.0) {
  geometry_msgs::msg::Point result;
  result.x = x;
  result.y = y;
  result.z = z;
  return result;
}

geometry_msgs::msg::Point rotate(const geometry_msgs::msg::Quaternion &q,
                                 const geometry_msgs::msg::Point &p) {
  // v' = v + 2 w (q_xyz x v) + 2 (q_xyz x (q_xyz x v)).
  const double tx = 2.0 * (q.y * p.z - q.z * p.y);
  const double ty = 2.0 * (q.z * p.x - q.x * p.z);
  const double tz = 2.0 * (q.x * p.y - q.y * p.x);
  return point(p.x + q.w * tx + q.y * tz - q.z * ty,
               p.y + q.w * ty + q.z * tx - q.x * tz,
               p.z + q.w * tz + q.x * ty - q.y * tx);
}

void triangle(visualization_msgs::msg::Marker &marker,
              const geometry_msgs::msg::Point &a,
              const geometry_msgs::msg::Point &b,
              const geometry_msgs::msg::Point &c) {
  marker.points.insert(marker.points.end(), {a, b, c});
}

void quad(visualization_msgs::msg::Marker &marker,
          const geometry_msgs::msg::Point &a,
          const geometry_msgs::msg::Point &b,
          const geometry_msgs::msg::Point &c,
          const geometry_msgs::msg::Point &d) {
  triangle(marker, a, b, c);
  triangle(marker, a, c, d);
}
}  // namespace

class AttitudeVisualizer final : public rclcpp::Node {
 public:
  AttitudeVisualizer() : Node("vn100_attitude_visualizer") {
    const auto topic = declare_parameter<std::string>("imu_topic", "/vn100/imu/data_ned");
    publisher_ = create_publisher<visualization_msgs::msg::MarkerArray>(
        "/vn100/attitude_markers", 10);
    subscriber_ = create_subscription<sensor_msgs::msg::Imu>(
        topic, rclcpp::SensorDataQoS(),
        [this](sensor_msgs::msg::Imu::ConstSharedPtr imu) { onImu(*imu); });
    RCLCPP_INFO(get_logger(), "Displaying gravity-derived roll/pitch from %s",
                topic.c_str());
  }

 private:
  void onImu(const sensor_msgs::msg::Imu &imu) {
    // The driver reports body axes in NED (forward/right/down). Convert the
    // measured specific force to vehicle forward/left/up for the car model.
    const double ax = imu.linear_acceleration.x;
    const double ay = -imu.linear_acceleration.y;
    const double az = -imu.linear_acceleration.z;
    const double magnitude = std::hypot(ax, std::hypot(ay, az));
    if (!std::isfinite(magnitude) || magnitude < 1.0) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                           "No usable accelerometer vector; cannot display tilt");
      return;
    }

    // Low-pass filtering calms sensor noise. Linear acceleration still affects
    // the apparent tilt, so this is intended for a stationary hand test.
    constexpr double alpha = 0.15;
    if (!have_acceleration_) {
      filtered_x_ = ax;
      filtered_y_ = ay;
      filtered_z_ = az;
      have_acceleration_ = true;
    } else {
      filtered_x_ += alpha * (ax - filtered_x_);
      filtered_y_ += alpha * (ay - filtered_y_);
      filtered_z_ += alpha * (az - filtered_z_);
    }

    const double roll = std::atan2(filtered_y_, filtered_z_);
    const double pitch = std::atan2(
        -filtered_x_, std::hypot(filtered_y_, filtered_z_));
    const double half_roll = roll / 2.0;
    const double half_pitch = pitch / 2.0;
    geometry_msgs::msg::Quaternion attitude;
    // R = Ry(pitch) Rx(roll): yaw is fixed at zero, not estimated.
    attitude.x = std::sin(half_roll) * std::cos(half_pitch);
    attitude.y = std::cos(half_roll) * std::sin(half_pitch);
    attitude.z = -std::sin(half_roll) * std::sin(half_pitch);
    attitude.w = std::cos(half_roll) * std::cos(half_pitch);

    auto marker = [&](int id, int type) {
      visualization_msgs::msg::Marker result;
      result.header.frame_id = "vn100_visualization";
      result.header.stamp = now();
      result.ns = "vn100_imu";
      result.id = id;
      result.type = type;
      result.action = visualization_msgs::msg::Marker::ADD;
      result.pose.orientation.w = 1.0;
      result.scale.x = result.scale.y = result.scale.z = 1.0;
      result.color.a = 1.0;
      return result;
    };

    // Vehicle axes: +X front (headlights), +Y left, +Z up.
    auto body = marker(0, visualization_msgs::msg::Marker::CUBE);
    body.pose.orientation = attitude;
    body.pose.position = rotate(attitude, point(0.05, 0, 0.26));
    body.scale.x = 1.80;
    body.scale.y = 0.70;
    body.scale.z = 0.26;
    body.color.r = 0.08;
    body.color.g = 0.42;
    body.color.b = 0.95;

    auto glass = marker(1, visualization_msgs::msg::Marker::TRIANGLE_LIST);
    glass.pose.orientation = attitude;
    glass.color.r = 0.28;
    glass.color.g = 0.44;
    glass.color.b = 0.53;
    // Sloping windshield, rear window and side windows.
    quad(glass, point(0.48, -0.33, 0.39), point(0.48, 0.33, 0.39),
         point(0.20, 0.28, 0.67), point(0.20, -0.28, 0.67));
    quad(glass, point(-0.70, 0.33, 0.39), point(-0.70, -0.33, 0.39),
         point(-0.46, -0.28, 0.67), point(-0.46, 0.28, 0.67));
    quad(glass, point(0.48, 0.33, 0.39), point(-0.70, 0.33, 0.39),
         point(-0.46, 0.28, 0.67), point(0.20, 0.28, 0.67));
    quad(glass, point(-0.70, -0.33, 0.39), point(0.48, -0.33, 0.39),
         point(0.20, -0.28, 0.67), point(-0.46, -0.28, 0.67));

    auto roof = marker(2, visualization_msgs::msg::Marker::CUBE);
    roof.pose.orientation = attitude;
    roof.pose.position = rotate(attitude, point(-0.13, 0, 0.68));
    roof.scale.x = 0.68;
    roof.scale.y = 0.58;
    roof.scale.z = 0.04;
    roof.color = body.color;

    auto headlights = marker(3, visualization_msgs::msg::Marker::TRIANGLE_LIST);
    headlights.pose.orientation = attitude;
    headlights.color.r = 1.0;
    headlights.color.g = 0.98;
    headlights.color.b = 0.75;
    auto taillights = marker(14, visualization_msgs::msg::Marker::TRIANGLE_LIST);
    taillights.pose.orientation = attitude;
    taillights.color.r = 1.0;
    taillights.color.g = 0.04;
    taillights.color.b = 0.02;
    for (const double y : {-0.25, 0.25}) {
      quad(headlights, point(0.951, y - 0.07, 0.25), point(0.951, y + 0.07, 0.25),
           point(0.951, y + 0.07, 0.35), point(0.951, y - 0.07, 0.35));
      quad(taillights, point(-0.851, y + 0.07, 0.25), point(-0.851, y - 0.07, 0.25),
           point(-0.851, y - 0.07, 0.35), point(-0.851, y + 0.07, 0.35));
    }

    auto arcs = marker(4, visualization_msgs::msg::Marker::LINE_LIST);
    arcs.pose.orientation = attitude;
    arcs.color.r = 1.0;
    arcs.color.g = 0.40;
    arcs.color.b = 0.04;
    arcs.scale.x = 0.025;
    auto arrowheads = marker(5, visualization_msgs::msg::Marker::TRIANGLE_LIST);
    arrowheads.pose.orientation = attitude;
    arrowheads.color = arcs.color;

    auto add_arc = [&](const geometry_msgs::msg::Point &origin,
                       const geometry_msgs::msg::Point &u,
                       const geometry_msgs::msg::Point &v,
                       double radius, double start, double end) {
      auto position = [&](double angle) {
        return point(origin.x + radius * (std::cos(angle) * u.x + std::sin(angle) * v.x),
                     origin.y + radius * (std::cos(angle) * u.y + std::sin(angle) * v.y),
                     origin.z + radius * (std::cos(angle) * u.z + std::sin(angle) * v.z));
      };
      constexpr int segments = 22;
      for (int i = 0; i < segments; ++i) {
        const double a = start + (end - start) * i / segments;
        const double b = start + (end - start) * (i + 1) / segments;
        arcs.points.push_back(position(a));
        arcs.points.push_back(position(b));
      }
      const auto tip = position(end);
      const auto tangent = point(-std::sin(end) * u.x + std::cos(end) * v.x,
                                 -std::sin(end) * u.y + std::cos(end) * v.y,
                                 -std::sin(end) * u.z + std::cos(end) * v.z);
      const auto radial = point(std::cos(end) * u.x + std::sin(end) * v.x,
                                std::cos(end) * u.y + std::sin(end) * v.y,
                                std::cos(end) * u.z + std::sin(end) * v.z);
      const auto left = point(tip.x - 0.14 * tangent.x + 0.065 * radial.x,
                              tip.y - 0.14 * tangent.y + 0.065 * radial.y,
                              tip.z - 0.14 * tangent.z + 0.065 * radial.z);
      const auto right = point(tip.x - 0.14 * tangent.x - 0.065 * radial.x,
                               tip.y - 0.14 * tangent.y - 0.065 * radial.y,
                               tip.z - 0.14 * tangent.z - 0.065 * radial.z);
      triangle(arrowheads, tip, left, right);
    };
    // Curved guides about the body X (roll) and Y (pitch) axes.
    add_arc(point(1.14, 0, 0), point(0, 1, 0), point(0, 0, 1),
            0.27, -1.2, 2.4);
    // X cross -Z = +Y: follow the same positive rotation as Ry(pitch).
    add_arc(point(-0.15, 1.08, 0), point(1, 0, 0), point(0, 0, -1),
            0.30, -2.4, 1.0);

    auto arrow_label = [&](int id, const std::string &text,
                           const geometry_msgs::msg::Point &location) {
      auto label = marker(id, visualization_msgs::msg::Marker::TEXT_VIEW_FACING);
      label.pose.position = rotate(attitude, location);
      label.color = arcs.color;
      label.scale.z = 0.10;
      label.text = text;
      return label;
    };
    auto angle_text = [](const char *name, double radians) {
      std::ostringstream stream;
      const double degrees = radians * kRadiansToDegrees;
      stream << name << ':' << std::fixed << std::setprecision(1)
             << (std::abs(degrees) < 0.05 ? 0.0 : degrees) << "deg";
      return stream.str();
    };
    auto roll_label = arrow_label(7, angle_text("ROLL", roll),
                                  point(1.00, -0.30, 0.65));
    auto pitch_label = arrow_label(8, angle_text("PITCH", pitch),
                                   point(-0.15, 1.10, 1.05));

    visualization_msgs::msg::MarkerArray output;
    output.markers = {std::move(body), std::move(glass), std::move(roof),
                      std::move(headlights), std::move(taillights), std::move(arcs),
                      std::move(arrowheads), std::move(roll_label), std::move(pitch_label)};
    int wheel_id = 10;
    for (const double x : {-0.57, 0.60}) {
      for (const double y : {-0.37, 0.37}) {
        auto wheel = marker(wheel_id++, visualization_msgs::msg::Marker::CYLINDER);
        wheel.pose.position = rotate(attitude, point(x, y, 0.16));
        // Cylinder axis Z -> wheel axle Y, followed by vehicle attitude.
        const double s = std::sqrt(0.5);
        wheel.pose.orientation.x = s * (attitude.w + attitude.x);
        wheel.pose.orientation.y = s * (attitude.y + attitude.z);
        wheel.pose.orientation.z = s * (attitude.z - attitude.y);
        wheel.pose.orientation.w = s * (attitude.w - attitude.x);
        wheel.scale.x = wheel.scale.y = 0.40;
        wheel.scale.z = 0.14;
        wheel.color.r = wheel.color.g = wheel.color.b = 0.16;
        output.markers.push_back(std::move(wheel));
      }
    }
    publisher_->publish(output);
  }

  bool have_acceleration_ = false;
  double filtered_x_ = 0.0, filtered_y_ = 0.0, filtered_z_ = 0.0;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr subscriber_;
  rclcpp::Publisher<visualization_msgs::msg::MarkerArray>::SharedPtr publisher_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<AttitudeVisualizer>());
  rclcpp::shutdown();
  return 0;
}
