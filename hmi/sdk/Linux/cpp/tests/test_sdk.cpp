#include <robot_sdk/api.hpp>
#include "transport/framing.hpp"
#include "transport/socket.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>

using json = nlohmann::json;
using namespace std::chrono_literals;
int checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) throw std::runtime_error("check failed: " #value); } while (false)

namespace {
void closeSocket(robot_sdk::NativeSocket fd) {
#ifdef _WIN32
    ::closesocket(fd);
#else
    ::close(fd);
#endif
}
json envelope(const char *type, const std::string &channel, json payload) {
    return json{{"v",1},{"t",type},{"ch",channel},{"ts",1.0},{"robot","SDK-test"},{"p",payload}};
}
void sendJson(robot_sdk::NativeSocket fd, const json &value) {
    const auto raw = inspection::encodeFrame(value.dump());
    std::size_t sent = 0;
    while (sent < raw.size()) {
#ifdef MSG_NOSIGNAL
        auto amount = ::send(fd, raw.data() + sent, raw.size() - sent, MSG_NOSIGNAL);
#else
        auto amount = ::send(fd, raw.data() + sent, static_cast<int>(raw.size() - sent), 0);
#endif
        if (amount <= 0) return;
        sent += static_cast<std::size_t>(amount);
    }
}
class Peer {
    robot_sdk::NativeSocket listener_ = robot_sdk::kInvalidSocket;
    std::thread thread_;
public:
    std::uint16_t port = 0;
    std::vector<json> observed;
    std::exception_ptr error;
    using Handler = std::function<void(robot_sdk::NativeSocket, const json &)>;
    explicit Peer(Handler handler) {
        CHECK(robot_sdk::SocketLibrary::ensureStarted());
        listener_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        CHECK(listener_ != robot_sdk::kInvalidSocket);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
        CHECK(::listen(listener_, 1) == 0);
#ifdef _WIN32
        int size = sizeof address;
#else
        socklen_t size = sizeof address;
#endif
        CHECK(::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) == 0);
        port = ntohs(address.sin_port);
        thread_ = std::thread([this, handler] {
            const auto fd = ::accept(listener_, nullptr, nullptr);
            if (fd == robot_sdk::kInvalidSocket) return;
            try {
                inspection::FrameDecoder decoder;
                char data[16384];
                for (;;) {
                    const auto amount = ::recv(fd, data, sizeof data, 0);
                    if (amount <= 0) break;
                    decoder.append(data, static_cast<std::size_t>(amount));
                    for (;;) {
                        inspection::Frame frame;
                        if (decoder.next(frame) != inspection::DecodeStatus::Ok) break;
                        const auto value = json::parse(frame.header);
                        if (value["t"] == "hb") continue;
                        observed.push_back(value);
                        handler(fd, value);
                    }
                }
            } catch (...) { error = std::current_exception(); }
            closeSocket(fd);
        });
    }
    void join() {
        if (thread_.joinable()) thread_.join();
        if (error) std::rethrow_exception(error);
    }
    ~Peer() { closeSocket(listener_); if (thread_.joinable()) thread_.join(); }
};
void reply(robot_sdk::NativeSocket fd, const json &request) {
    auto result = envelope("res", request["ch"], json{{"ok",true}});
    result["id"] = request["id"];
    sendJson(fd, result);
}
void framing() {
    const auto frame = inspection::encodeFrame("{}", std::string("\0SHLM",5));
    inspection::FrameDecoder decoder;
    inspection::Frame out;
    for (std::size_t i = 0; i < frame.size(); ++i) {
        decoder.append(frame.data() + i, 1);
        CHECK(decoder.next(out) == (i + 1 == frame.size() ? inspection::DecodeStatus::Ok : inspection::DecodeStatus::NeedMore));
    }
    CHECK(out.header == "{}");
    CHECK(out.payload == std::string("\0SHLM",5));
    bool rejected = false;
    try { inspection::encodeFrame(std::string(inspection::kMaxBodyLen, 'x')); }
    catch (const std::length_error &) { rejected = true; }
    CHECK(rejected);
}
void synchronousAndCache() {
    Peer peer([](auto fd, const auto &request) {
        sendJson(fd, envelope("pub", "state/pose", json{{"x",2.0}}));
        reply(fd, request);
    });
    robot_sdk::Client client; std::string error;
    CHECK(client.connect("127.0.0.1", peer.port, &error));
    int states = 0;
    auto result = client.request("cmd/maps/list", "{}", 1000, [&](const auto &message) {
        if (message.channel == "state/pose") ++states;
        return true;
    }, &error);
    CHECK(result && result->ok);
    CHECK(states == 1);
    CHECK(client.robotId() == "SDK-test");
    CHECK(client.latest("state/pose"));
    CHECK(json::parse(client.latest("state/pose")->envelope)["p"]["x"] == 2.0);
    CHECK(!client.publish("cmd/cmd_vel", "{}", &error));
    CHECK(client.sendRequest("cmd/maps/list", "[]", &error).empty());
    CHECK(client.isConnected());
    client.close();
    CHECK(client.robotId().empty());
    CHECK(!client.latest("state/pose"));
    peer.join();
}
void invalidEnvelopes() {
    const std::vector<json> changes = {json{{"v",true}}, json{{"v",1.0}}, json{{"p",json::array()}},
        json{{"ch",42}}, json{{"ts",true}}, json{{"robot",""}}, json{{"t","unknown"}}};
    for (const auto &change : changes) {
        Peer peer([change](auto fd, const auto &) {
            auto value = envelope("pub", "state/pose", json::object());
            value.update(change); sendJson(fd,value);
        });
        robot_sdk::Client client; std::string error;
        CHECK(client.connect("127.0.0.1", peer.port, &error));
        CHECK(!client.request("cmd/maps/list", "{}", 500, {}, &error));
        CHECK(!client.isConnected());
        CHECK(!error.empty());
        peer.join();
    }
}
void mismatchAndTimeout() {
    for (bool wrongRobot : {false,true}) {
        Peer peer([wrongRobot](auto fd, const auto &request) {
            sendJson(fd, envelope("pub","state/pose",json::object()));
            auto value = envelope("res", wrongRobot ? request["ch"].template get<std::string>() : "cmd/mode", json{{"ok",true}});
            value["id"] = request["id"];
            if (wrongRobot) value["robot"] = "Other";
            sendJson(fd,value);
        });
        robot_sdk::Client client; std::string error;
        CHECK(client.connect("127.0.0.1",peer.port,&error));
        CHECK(!client.request("cmd/maps/list","{}",500,{},&error));
        CHECK(!client.isConnected()); client.close(); peer.join();
    }
    Peer peer([](auto, const auto &) {});
    robot_sdk::Client client; std::string error;
    CHECK(client.connect("127.0.0.1",peer.port,&error));
    CHECK(!client.request("cmd/maps/list","{}",250,{},&error));
    CHECK(client.isConnected());
    CHECK(error.find("timed out") != std::string::npos);
    client.close(); peer.join(); CHECK(peer.observed.size() == 1);
}
void apiPayloads() {
    Peer peer(reply);
    robot_sdk::Client client; std::string error;
    CHECK(client.connect("127.0.0.1",peer.port,&error));
    robot_sdk::RobotApi api(client);
    std::vector<std::string> ids;
    ids.push_back(api.startMission("mission",&error));
    ids.push_back(api.pauseNavigation(&error));
    ids.push_back(api.resumeNavigation(&error));
    ids.push_back(api.setSpeedLimits(0.3,0.5,&error));
    ids.push_back(api.setSpeedRanges(0.1,0.6,0.05,0.8,&error));
    ids.push_back(api.setInitialPose({1,2,0.5},&error));
    ids.push_back(api.triggerCapture("GTXA","1234","05","P1",7,&error));
    ids.push_back(api.setWaypoints({{"wp",{1,2,0.5},"Waypoint","description"}},"map","[]",&error));
    ids.push_back(api.setMarkers({{7,{1,2,0.5},1.2,"wall"}},"map","[]",&error));
    ids.push_back(api.saveMission(R"({"id":"m","name":"Mission","steps":[]})","map",0,&error));
    ids.push_back(api.saveArmPose(R"({"id":"arm","name":"Arm","positions":[0,0,0,0,0,0]})",&error));
    ids.push_back(api.setDefaultMap("",&error));
    ids.push_back(api.setBasePosture("damp",true,&error));
    for (const auto &id : ids) {
        CHECK(!id.empty());
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        bool received = false;
        while (std::chrono::steady_clock::now() < deadline && !received) {
            std::vector<robot_sdk::Message> messages;
            CHECK(client.poll(messages,200,&error));
            if (auto result = client.takeResponse(id)) { CHECK(result->ok); received = true; }
        }
        CHECK(received);
    }
    CHECK(api.setBasePosture("damp",false,&error).empty());
    CHECK(api.armJointGoal({0,0},&error).empty());
    CHECK(api.saveMission(R"({"id":"m","name":"Mission","map_id":"other","steps":[]})","map",0,&error).empty());
    CHECK(api.saveArmPose(R"({"id":"arm","name":"Arm","positions":[0,0]})",&error).empty());
    client.close(); peer.join();
    CHECK(peer.observed.size() == ids.size());
    CHECK(peer.observed[0]["p"]["mission_id"] == "mission");
    CHECK(peer.observed[6]["p"]["train_number"] == "1234");
    CHECK(peer.observed[7]["p"]["map_id"] == "map");
    CHECK(peer.observed[8]["p"]["markers"][0]["z"] == 1.2);
    CHECK(peer.observed[8]["p"]["markers"][0]["yaw"] == 0.5);
    CHECK(!peer.observed[8]["p"]["markers"][0].contains("theta"));
}
}
int main() {
    try { framing(); synchronousAndCache(); invalidEnvelopes(); mismatchAndTimeout(); apiPayloads(); }
    catch (const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
    std::printf("PASS: %d checks\n",checks);
    return 0;
}
