// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <memory>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/imu.hpp>

class Vn100ImuAdapter final : public rclcpp::Node
{
public:
    Vn100ImuAdapter() : Node("imu_adapter")
    {
        auto qos = rclcpp::SensorDataQoS();
        publisher_ = create_publisher<sensor_msgs::msg::Imu>("imu/data_ned", qos);
        subscription_ = create_subscription<sensor_msgs::msg::Imu>(
            "vectornav_driver_node/imu/data", qos,
            [this](sensor_msgs::msg::Imu::ConstSharedPtr raw) {
                sensor_msgs::msg::Imu imu = *raw;
                // The driver does not put an attitude estimate in its raw IMU message.
                // ROS consumers must see that orientation is unavailable.
                imu.orientation.x = 0.0;
                imu.orientation.y = 0.0;
                imu.orientation.z = 0.0;
                imu.orientation.w = 1.0;
                imu.orientation_covariance.fill(0.0);
                imu.orientation_covariance[0] = -1.0;
                publisher_->publish(imu);
            });
    }

private:
    rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr subscription_;
    rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr publisher_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<Vn100ImuAdapter>());
    rclcpp::shutdown();
    return 0;
}
