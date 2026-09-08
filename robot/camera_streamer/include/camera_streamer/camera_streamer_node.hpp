#pragma once

// Colour frames from a ROS topic, H.264 over RTSP.
//
// The station's viewfinder. Real inspection photographs are taken from a
// standstill and go to the NAS as originals (statement of work 2.2.4), so
// this path is judged on latency, not quality: when the picture lags, the
// operator keeps moving the arm and overshoots.
//
// Why not the existing TCP bridge
// -------------------------------
// TCP delivers every frame, late. That is the wrong trade here - a stale
// frame is worse than a dropped one. It would also share one outbound buffer
// with pose, health and navigation, so video congestion would delay the
// things the operator steers by.
//
// Why RTSP and not a raw UDP socket
// ---------------------------------
// The port layout has to be written into a firewall rule and proven closed
// with a packet capture (statement of work 7.1). RTSP normally negotiates its
// RTP ports, which would make that proof messy, so the range is pinned here
// and the server binds one interface rather than every one.

#include <memory>
#include <string>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_srvs/srv/set_bool.hpp>

namespace camera_streamer {

class CameraStreamerNode : public rclcpp::Node {
public:
    /// One of the named quality presets. Unknown names fall back to the
    /// default and say so - a typo in a config should not leave the operator
    /// with no picture and no reason.
    struct Quality {
        const char *name;
        int width;
        int height;
        int fps;
        int bitrateKbps;
    };
    explicit CameraStreamerNode(const rclcpp::NodeOptions &options);
    ~CameraStreamerNode() override;

private:
    void onImage(const sensor_msgs::msg::Image::ConstSharedPtr &msg);
    void publishState();

    /// Builds the pipeline description. Split out so the encoder can be
    /// swapped without touching the rest: the Jetson has NVENC, a development
    /// machine does not.
    std::string encoderChain() const;

    static const Quality *findQuality(const std::string &name);
    const Quality &quality() const;

    /// Rebuilds the stream with the current preset. Clients reconnect.
    void applyQuality(const std::string &name);

    bool startServer();
    void stopServer();

    GstRTSPServer *server_ = nullptr;
    GstElement *appsrc_ = nullptr;
    guint serverId_ = 0;
    GMainLoop *loop_ = nullptr;
    std::unique_ptr<std::thread> loopThread_;

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
    rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr enableSrv_;
    rclcpp::TimerBase::SharedPtr stateTimer_;

    std::string encoder_;
    std::string mountPoint_;
    std::string bindAddress_;
    int port_ = 8554;
    std::string quality_ = "high";
    std::string pendingQuality_;
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr paramCb_;
    int bitrateKbps_ = 4000;
    int keyframeInterval_ = 15;
    double maxFps_ = 15.0;

    // 첫 프레임에서 읽는다. appsrc 의 caps 에 크기가 없으면 인코더와 협상이
    // 되지 않아 RTSP 가 503 을 돌려준다.
    int width_ = 0;
    int height_ = 0;

    rclcpp::Time lastFrame_;
    std::uint64_t framesIn_ = 0;
    std::uint64_t framesPushed_ = 0;
    std::string lastError_;
    bool streaming_ = false;
    bool autostart_ = false;
};

}  // namespace camera_streamer
