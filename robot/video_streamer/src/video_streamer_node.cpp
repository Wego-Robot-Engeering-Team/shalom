#include "video_streamer/video_streamer_node.hpp"

#include <thread>

namespace video_streamer {

namespace {

/// appsrc 이름. RTSP 미디어가 만들어질 때 이 이름으로 찾아 잡는다.
constexpr const char *kAppSrcName = "rosframe";

/// 미디어가 만들어질 때마다 불린다. RTSP 는 클라이언트가 붙을 때 파이프라인을
/// 새로 만들므로, 그때마다 appsrc 를 다시 잡아야 한다.
void onMediaConfigure(GstRTSPMediaFactory *, GstRTSPMedia *media, gpointer user)
{
    auto *self = static_cast<GstElement **>(user);
    GstElement *bin = gst_rtsp_media_get_element(media);
    if (!bin)
        return;
    if (*self)
        gst_object_unref(*self);
    *self = gst_bin_get_by_name_recurse_up(GST_BIN(bin), kAppSrcName);
    gst_object_unref(bin);
}

}  // namespace

// 화질 프리셋.
//
// 해상도를 바꾸려고 카메라를 다시 열지는 않는다. 장치를 놓았다 잡는 동안
// 화면이 몇 초 비고, 그 사이 촬영도 못 한다. 대신 인코더 앞에서 줄인다 —
// 카메라는 늘 같은 설정으로 돌고 파이프라인만 바뀐다.
//
// 뷰파인더의 값은 화질이 아니라 지연이다. 실제 점검 사진은 정지 상태에서
// 원본으로 찍어 NAS 로 가므로(과업지시서 2.2.4), 링크가 좁으면 화질을
// 버리는 편이 옳다.
const VideoStreamerNode::Quality kQualities[] = {
    {"high",  1280, 720, 15, 4000},   // 기본. 여유 있을 때.
    {"low",    848, 480, 30, 3000},   // 프레임을 올려 조작감을 얻는다.
    {"saver",  640, 360, 15, 1500},   // 무선이 좁을 때.
};

const VideoStreamerNode::Quality *VideoStreamerNode::findQuality(const std::string &name)
{
    for (const auto &q : kQualities)
        if (name == q.name)
            return &q;
    return nullptr;
}

const VideoStreamerNode::Quality &VideoStreamerNode::quality() const
{
    if (const Quality *q = findQuality(quality_))
        return *q;
    return kQualities[0];
}

VideoStreamerNode::VideoStreamerNode(const rclcpp::NodeOptions &options)
    : rclcpp::Node("video_streamer", options), lastFrame_(now())
{
    // 인코더는 바꿔 낄 수 있어야 한다. 젯슨에는 NVENC 가 있고 개발 PC 에는
    // 없다. 개발 PC 의 x264enc 는 libx264(GPL-2+) 를 링크하므로 검증에만
    // 쓰고 납품 파이프라인에는 절대 들어가면 안 된다 — "NVENC 가 없으면
    // x264 로" 같은 폴백을 넣는 순간 로봇 소프트웨어 전체가 GPL 이 된다.
    encoder_ = declare_parameter("encoder", std::string("nvv4l2h264enc"));
    mountPoint_ = declare_parameter("mount_point", std::string("/arm-rgb"));
    // 0.0.0.0 으로 두면 모든 인터페이스에서 듣는다. 과업지시서 7.1 이 외부
    // 통신 차단을 패킷 캡처로 검증하라고 하므로, 내부망 주소를 명시한다.
    bindAddress_ = declare_parameter("bind_address", std::string("127.0.0.1"));
    port_ = int(declare_parameter("port", port_));
    // 화질은 프리셋 이름으로 고른다. 폭·높이·프레임·비트레이트를 따로 받으면
    // 관제와 로봇이 서로 다른 조합을 들고 있게 되고, 어느 쪽이 맞는지 알 수
    // 없어진다.
    quality_ = declare_parameter("quality", quality_);
    if (!findQuality(quality_)) {
        RCLCPP_WARN(get_logger(), "모르는 화질 '%s' — 기본값으로 돈다", quality_.c_str());
        quality_ = kQualities[0].name;
    }
    bitrateKbps_ = int(declare_parameter("bitrate_kbps", quality().bitrateKbps));
    keyframeInterval_ = int(declare_parameter("keyframe_interval", keyframeInterval_));
    maxFps_ = declare_parameter("max_fps", maxFps_);
    const auto topic = declare_parameter("image_topic", std::string("color/image_raw"));
    // 첫 프레임이 오면 스스로 켠다. 크기를 알아야 파이프라인을 세울 수
    // 있으므로 여기서 바로 켤 수는 없다.
    autostart_ = declare_parameter("autostart", false);

    if (!gst_is_initialized())
        gst_init(nullptr, nullptr);

    // BEST_EFFORT 가 아니라 RELIABLE 이다. 카메라 토픽의 관례는 BEST_EFFORT
    // 지만, 720p RGB8 한 장이 2.76 MB 라 CycloneDDS 가 루프백 UDP 로 수백
    // 조각을 낸다. BEST_EFFORT 면 그중 하나만 빠져도 메시지가 통째로
    // 사라지는데, 실제로 그렇게 한 장도 안 들어왔다.
    //
    // 깊이는 1 이다. 밀린 프레임은 어차피 버릴 것이라 쌓아 둘 이유가 없다.
    //
    // 이 문제는 realsense2_camera 와 한 컨테이너에 합성하면 사라진다 —
    // intra-process 로 넘어가 DDS 를 타지 않는다. 이 설정은 노드를 따로
    // 띄웠을 때를 위한 것이다.
    sub_ = create_subscription<sensor_msgs::msg::Image>(
        topic, rclcpp::QoS(1).reliable(),
        [this](const sensor_msgs::msg::Image::ConstSharedPtr &msg) { onImage(msg); });

    // 뷰파인더는 필요할 때만 돈다. 자율주행 중에는 대역폭이 항법의 것이고,
    // 어차피 촬영은 정지 상태에서만 한다(과업지시서 2.2.4).
    enableSrv_ = create_service<std_srvs::srv::SetBool>(
        "~/enable",
        [this](const std_srvs::srv::SetBool::Request::SharedPtr req,
               std_srvs::srv::SetBool::Response::SharedPtr res) {
            res->success = req->data ? startServer() : (stopServer(), true);
            res->message = lastError_;
        });

    // 관제가 화질을 바꾸면 파라미터로 온다. 파이프라인을 다시 세워야 해서
    // 클라이언트는 잠깐 끊겼다 붙는다 — 몇 초 사이의 일이고, 대신 카메라를
    // 놓지 않으므로 촬영은 그 동안에도 된다.
    paramCb_ = add_on_set_parameters_callback(
        [this](const std::vector<rclcpp::Parameter> &params) {
            rcl_interfaces::msg::SetParametersResult res;
            res.successful = true;
            for (const auto &p : params) {
                if (p.get_name() != "quality")
                    continue;
                const std::string want = p.as_string();
                if (!findQuality(want)) {
                    res.successful = false;
                    res.reason = "모르는 화질: " + want;
                    return res;
                }
                // 여기서 바로 다시 세우면 파라미터 콜백 안에서 GStreamer 를
                // 만지게 된다. 타이머로 미뤄 콜백을 먼저 끝낸다.
                pendingQuality_ = want;
            }
            return res;
        });

    stateTimer_ = create_wall_timer(std::chrono::seconds(1), [this] {
        if (!pendingQuality_.empty()) {
            const std::string want = pendingQuality_;
            pendingQuality_.clear();
            applyQuality(want);
        }
        publishState();
    });

    RCLCPP_INFO(get_logger(), "영상 송신 준비: %s → rtsp://%s:%d%s (인코더 %s)",
                topic.c_str(), bindAddress_.c_str(), port_, mountPoint_.c_str(),
                encoder_.c_str());
}

VideoStreamerNode::~VideoStreamerNode()
{
    stopServer();
}

std::string VideoStreamerNode::encoderChain() const
{
    // 저지연 설정은 기본값에 기대지 않고 전부 명시한다. JetPack 판본마다
    // 속성 기본값이 달라, 한쪽에서 되던 것이 다른 쪽에서 2 초씩 밀린다.
    if (encoder_.rfind("nvv4l2", 0) == 0) {
        return encoder_
               + " bitrate=" + std::to_string(bitrateKbps_ * 1000)
               + " control-rate=1"          // CBR
               + " insert-sps-pps=1"        // 중간에 붙어도 바로 그림이 뜬다
               + " num-B-Frames=0"          // B 프레임은 곧 지연이다
               + " iframeinterval=" + std::to_string(keyframeInterval_)
               + " maxperf-enable=1";
    }
    // 개발 검증용. tune=zerolatency 가 B 프레임과 lookahead 를 끈다.
    return encoder_
           + " bitrate=" + std::to_string(bitrateKbps_)
           + " tune=zerolatency speed-preset=veryfast"
           + " key-int-max=" + std::to_string(keyframeInterval_);
}

bool VideoStreamerNode::startServer()
{
    if (server_)
        return true;

    // 크기를 모르면 파이프라인을 세울 수 없다. appsrc 의 caps 에 width/height
    // 가 빠지면 인코더와 협상이 되지 않고, 증상은 클라이언트 쪽의 503 뿐이라
    // 원인을 짚기 어렵다. 여기서 먼저 막고 이유를 말한다.
    if (width_ <= 0 || height_ <= 0) {
        lastError_ = "카메라 프레임을 아직 받지 못했다 — 크기를 알 수 없다";
        RCLCPP_WARN(get_logger(), "%s", lastError_.c_str());
        return false;
    }

    server_ = gst_rtsp_server_new();
    gst_rtsp_server_set_address(server_, bindAddress_.c_str());
    gst_rtsp_server_set_service(server_, std::to_string(port_).c_str());

    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server_);
    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();

    // 카메라가 내는 크기 그대로 받아, 인코더 앞에서 프리셋 크기로 줄인다.
    // 카메라를 다시 열지 않으므로 화질을 바꿔도 촬영은 끊기지 않는다.
    const Quality &q = quality();
    const std::string launch =
        std::string("( appsrc name=") + kAppSrcName
        + " is-live=true do-timestamp=true format=time"
          " caps=video/x-raw,format=RGB"
          ",width=" + std::to_string(width_)
        + ",height=" + std::to_string(height_)
        + ",framerate=" + std::to_string(int(maxFps_ + 0.5)) + "/1 "
          "! videoconvert ! videoscale "
          "! video/x-raw,format=I420,width=" + std::to_string(q.width)
        + ",height=" + std::to_string(q.height) + " "
          "! " + encoderChain()
        + " ! h264parse config-interval=1 ! rtph264pay name=pay0 pt=96 )";

    gst_rtsp_media_factory_set_launch(factory, launch.c_str());
    // 여러 클라이언트가 붙어도 파이프라인은 하나만. 카메라가 하나이므로
    // 인코딩을 두 번 할 이유가 없다.
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    g_signal_connect(factory, "media-configure", G_CALLBACK(onMediaConfigure), &appsrc_);
    gst_rtsp_mount_points_add_factory(mounts, mountPoint_.c_str(), factory);
    g_object_unref(mounts);

    loop_ = g_main_loop_new(nullptr, FALSE);
    serverId_ = gst_rtsp_server_attach(server_, nullptr);
    if (serverId_ == 0) {
        lastError_ = "RTSP 서버를 " + bindAddress_ + ":" + std::to_string(port_)
                     + " 에 붙이지 못했다";
        RCLCPP_ERROR(get_logger(), "%s", lastError_.c_str());
        stopServer();
        return false;
    }

    loopThread_ = std::make_unique<std::thread>([this] { g_main_loop_run(loop_); });
    streaming_ = true;
    lastError_.clear();
    RCLCPP_INFO(get_logger(), "영상 송신 시작: rtsp://%s:%d%s",
                bindAddress_.c_str(), port_, mountPoint_.c_str());
    return true;
}

void VideoStreamerNode::applyQuality(const std::string &name)
{
    if (name == quality_)
        return;
    quality_ = name;
    bitrateKbps_ = quality().bitrateKbps;
    maxFps_ = quality().fps;
    RCLCPP_INFO(get_logger(), "화질 %s (%dx%d @%d, %d kbps)", quality().name,
                quality().width, quality().height, quality().fps, bitrateKbps_);
    // 돌고 있을 때만 다시 세운다. 꺼져 있으면 다음에 켤 때 새 값으로 뜬다.
    if (streaming_) {
        stopServer();
        startServer();
    }
}

void VideoStreamerNode::stopServer()
{
    streaming_ = false;
    if (loop_) {
        g_main_loop_quit(loop_);
        if (loopThread_ && loopThread_->joinable())
            loopThread_->join();
        loopThread_.reset();
        g_main_loop_unref(loop_);
        loop_ = nullptr;
    }
    if (serverId_) {
        g_source_remove(serverId_);
        serverId_ = 0;
    }
    if (appsrc_) {
        gst_object_unref(appsrc_);
        appsrc_ = nullptr;
    }
    if (server_) {
        g_object_unref(server_);
        server_ = nullptr;
    }
}

void VideoStreamerNode::onImage(const sensor_msgs::msg::Image::ConstSharedPtr &msg)
{
    ++framesIn_;
    lastFrame_ = now();

    // 스트림이 꺼져 있어도 크기는 기록한다 — 켤 때 이 값이 필요하다.
    if (msg->encoding == "rgb8" && (width_ != int(msg->width) || height_ != int(msg->height))) {
        if (width_ != 0)
            RCLCPP_WARN(get_logger(), "프레임 크기가 %dx%d 에서 %ux%u 로 바뀌었다 — "
                        "스트림을 다시 켜야 반영된다", width_, height_, msg->width, msg->height);
        width_ = int(msg->width);
        height_ = int(msg->height);
    }

    if (autostart_ && !streaming_ && width_ > 0) {
        autostart_ = false;   // 한 번만. 실패하면 서비스로 다시 켠다.
        startServer();
    }

    if (!streaming_ || !appsrc_)
        return;

    // 들어오는 주기를 그대로 밀지 않는다. 카메라가 30 fps 로 와도 뷰파인더는
    // 설정한 만큼만 보낸다 — 남는 대역폭은 항법이 쓴다.
    static rclcpp::Time lastPush(0, 0, RCL_ROS_TIME);
    const double minGap = maxFps_ > 0.0 ? 1.0 / maxFps_ : 0.0;
    if (lastPush.nanoseconds() != 0 && (lastFrame_ - lastPush).seconds() < minGap)
        return;
    lastPush = lastFrame_;

    if (msg->encoding != "rgb8") {
        lastError_ = "지원하지 않는 인코딩: " + msg->encoding;
        return;
    }

    GstBuffer *buf = gst_buffer_new_allocate(nullptr, gsize(msg->data.size()), nullptr);
    gst_buffer_fill(buf, 0, msg->data.data(), msg->data.size());
    GstFlowReturn ret = GST_FLOW_OK;
    g_signal_emit_by_name(appsrc_, "push-buffer", buf, &ret);
    gst_buffer_unref(buf);

    if (ret != GST_FLOW_OK)
        lastError_ = "push-buffer 실패";
    else
        ++framesPushed_;
}

void VideoStreamerNode::publishState()
{
    // 상태는 브릿지가 state/video 로 관제에 올린다. 여기서는 로그만 남긴다 —
    // 영상이 안 보일 때 "링크가 끊긴 것" 과 "카메라가 안 오는 것" 과
    // "인코더가 죽은 것" 이 구분돼야 원인을 짚을 수 있다.
    static std::uint64_t lastIn = 0, lastPushed = 0;
    const auto in = framesIn_ - lastIn;
    const auto pushed = framesPushed_ - lastPushed;
    lastIn = framesIn_;
    lastPushed = framesPushed_;

    if (streaming_ && in == 0)
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                             "송신 중인데 카메라 프레임이 오지 않는다");
    RCLCPP_DEBUG(get_logger(), "수신 %lu fps, 송출 %lu fps%s",
                 static_cast<unsigned long>(in), static_cast<unsigned long>(pushed),
                 lastError_.empty() ? "" : (" / " + lastError_).c_str());
}

}  // namespace video_streamer

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(video_streamer::VideoStreamerNode)
