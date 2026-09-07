#pragma once

// Bridge between the control station and the ROS 2 stack.
//
// This node is the boundary of the DDS domain. The control station does not
// run ROS 2: it connects over a single raw TCP socket, and everything it sees
// or commands passes through here. See docs/bridge_protocol.md.
//
// SAFETY IS NOT THIS NODE'S JOB
// -----------------------------
// The three-second communication-loss stop and the one-second emergency stop
// are enforced by a separate safety node. This one only reports liveness: it
// publishes a heartbeat topic while the control station's heartbeat is fresh,
// and stops publishing otherwise.
//
// The separation matters because *this node crashing must also stop the
// robot*. If the stop decision lived here, a segfault would leave the robot
// driving with nobody watching. Publishing liveness and letting another
// process act on its absence makes the failure safe by construction.
//
// THREADING
// ---------
// The TCP server runs its own thread and hands frames over through a queue.
// Everything in this class runs on the executor thread, so no ROS 2 handle is
// ever touched from the network thread.

#include <geometry_msgs/msg/twist.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <nav_msgs/msg/path.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <std_msgs/msg/bool.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>

#include <chrono>
#include <memory>
#include <string>

#include "shalom_bridge/envelope.hpp"
#include "shalom_bridge/tcp_server.hpp"

namespace shalom_bridge {

class BridgeNode : public rclcpp::Node {
public:
    BridgeNode();
    ~BridgeNode() override;

private:
    // ---- link ------------------------------------------------------------
    void pollLink();
    void handleFrame(const inspection::Frame &frame);
    void handleRequest(const Envelope &request);
    void handleHeartbeat(const Envelope &heartbeat);

    void sendEnvelope(const Envelope &env, bool lossy = false);
    void respond(const Envelope &request, bool ok, const std::string &code = {},
                 const std::string &message = {});

    /// Publishes the control station's liveness for the safety node, and
    /// latches the jog command to zero when it stops arriving.
    void tickSafety();

    // ---- telemetry -------------------------------------------------------
    void publishPose();
    void publishHealth();

    // ---- commands --------------------------------------------------------
    bool commandsAllowed(const Envelope &request);
    void applyCmdVel(const json &payload);

    // ---- parameters ------------------------------------------------------
    int port_ = 9090;
    std::string mapFrame_ = "map";
    std::string baseFrame_ = "base_link";

    /// Jog commands stop being honoured this long after the last one arrives.
    /// Matches the value published in the protocol, and both must change
    /// together.
    std::chrono::milliseconds deadman_{300};

    /// The control station is considered present while its heartbeat is no
    /// older than this. The safety node's own three-second rule is separate
    /// and deliberately longer.
    std::chrono::milliseconds heartbeatTimeout_{1000};

    // ---- state -----------------------------------------------------------
    TcpServer server_;
    bool estopEngaged_ = false;
    bool manualMode_ = false;
    rclcpp::Time lastHeartbeat_;
    rclcpp::Time lastCmdVel_;
    geometry_msgs::msg::Twist pendingTwist_;
    bool haveJogCommand_ = false;
    std::int64_t seq_ = 0;

    // ---- ROS interfaces --------------------------------------------------
    rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmdVelPub_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr linkAlivePub_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr estopPub_;

    rclcpp::Subscription<sensor_msgs::msg::BatteryState>::SharedPtr batterySub_;
    rclcpp::Subscription<sensor_msgs::msg::JointState>::SharedPtr jointSub_;
    rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr planSub_;
    rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr mapSub_;

    std::unique_ptr<tf2_ros::Buffer> tfBuffer_;
    std::shared_ptr<tf2_ros::TransformListener> tfListener_;

    rclcpp::TimerBase::SharedPtr linkTimer_;
    rclcpp::TimerBase::SharedPtr poseTimer_;
    rclcpp::TimerBase::SharedPtr safetyTimer_;
    rclcpp::TimerBase::SharedPtr healthTimer_;
};

}  // namespace shalom_bridge
