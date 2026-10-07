// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary
#include "robot_sdk/client.hpp"
#include "transport/framing.hpp"
#include "transport/socket.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace robot_sdk {
namespace {
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
constexpr auto kHeartbeatPeriod = std::chrono::milliseconds(200);
double unixTime() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }
double monotonicTime() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
bool validChannel(const std::string &value) {
    return !value.empty() && value.size() <= 256 &&
        std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32; });
}
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
Response response(const Message &message) {
    const auto p = json::parse(message.envelope).at("p");
    require(p.contains("ok") && p["ok"].is_boolean(), "response ok must be bool");
    Response result{message.requestId, p["ok"].get<bool>(), {}, {}, message};
    if (p.contains("err") && !p["err"].is_null()) {
        const auto &error = p["err"];
        require(error.is_object() && error.contains("code") && error["code"].is_string() &&
                error.contains("msg") && error["msg"].is_string(), "invalid response error");
        result.errorCode = error["code"].get<std::string>();
        result.errorMessage = error["msg"].get<std::string>();
    }
    return result;
}
}

class Client::Impl {
public:
    TcpClient socket;
    inspection::FrameDecoder decoder;
    std::string robot;
    std::uint64_t heartbeatSequence = 0, requestSequence = 0;
    Clock::time_point nextHeartbeat;
    struct Pending { std::string channel; Clock::time_point deadline; };
    std::map<std::string, Pending> pending;
    std::map<std::string, Response> replies;
    std::map<std::string, Message> states;
    Stop lastStop = Stop::SocketError;
    bool requesting = false;

    void clear() {
        socket.close(); decoder.reset(); robot.clear(); pending.clear(); replies.clear();
        states.clear(); heartbeatSequence = 0;
    }
    bool send(json envelope, std::string *err) {
        if (!socket.isConnected()) { if (err) *err = "client is not connected"; return false; }
        try {
            if (!robot.empty()) envelope["robot"] = robot;
            const auto header = envelope.dump();
            if (header.size() > inspection::kMaxBodyLen - 4) throw std::runtime_error("frame exceeds 32 MiB");
            if (socket.sendAll(inspection::encodeFrame(header), err)) return true;
            clear(); return false;
        } catch (const std::exception &e) { if (err) *err = e.what(); return false; }
    }
    bool heartbeat(std::string *err) {
        if (Clock::now() < nextHeartbeat) return true;
        if (!send(json{{"v",1},{"t","hb"},{"ts",unixTime()},{"p",{{"seq",++heartbeatSequence}}}}, err)) return false;
        nextHeartbeat = Clock::now() + kHeartbeatPeriod; return true;
    }
    void expire() {
        for (auto it = pending.begin(); it != pending.end();) {
            if (it->second.deadline < Clock::now()) { replies.erase(it->first); it = pending.erase(it); }
            else ++it;
        }
    }
};

Client::Client() : impl_(std::make_unique<Impl>()) {}
Client::~Client() = default;
Client::Client(Client &&) noexcept = default;
Client &Client::operator=(Client &&) noexcept = default;
bool Client::connect(const std::string &host, std::uint16_t port, std::string *err, int timeoutMs) {
    close();
    if (err) err->clear();
    if (host.empty() || port == 0 || timeoutMs <= 0) { if (err) *err = "invalid host, port or timeout"; return false; }
    if (!impl_->socket.connect(host, port, err, timeoutMs)) return false;
    impl_->nextHeartbeat = Clock::now();
    return impl_->heartbeat(err);
}
void Client::close() { impl_->clear(); }
bool Client::isConnected() const { return impl_->socket.isConnected(); }
const std::string &Client::robotId() const { return impl_->robot; }
std::optional<Message> Client::latest(const std::string &channel) const {
    const auto found = impl_->states.find(channel);
    return found == impl_->states.end() ? std::nullopt : std::optional<Message>(found->second);
}
bool Client::poll(std::vector<Message> &messages, int timeoutMs, std::string *err) {
    messages.clear();
    if (err) err->clear();
    if (timeoutMs < 0 || !isConnected()) { if (err) *err = "invalid timeout or disconnected client"; return false; }
    impl_->expire();
    if (!impl_->heartbeat(err)) return false;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(impl_->nextHeartbeat - Clock::now()).count();
    std::string chunk;
    const auto status = impl_->socket.read(chunk, std::min(timeoutMs, static_cast<int>(std::max<std::int64_t>(0, remaining))));
    if (status == ReadStatus::Timeout) return impl_->heartbeat(err);
    if (status != ReadStatus::Data) {
        impl_->lastStop = status == ReadStatus::Closed ? Stop::PeerClosed : Stop::SocketError;
        if (err) *err = status == ReadStatus::Closed ? "robot closed connection" : "socket receive failed: " + TcpClient::lastError();
        close(); return false;
    }
    impl_->decoder.append(chunk);
    try {
        for (;;) {
            inspection::Frame frame;
            const auto decoded = impl_->decoder.next(frame);
            if (decoded == inspection::DecodeStatus::NeedMore) break;
            require(decoded == inspection::DecodeStatus::Ok, "invalid frame magic or length");
            const auto env = json::parse(frame.header);
            require(env.is_object() && env.contains("v") && env["v"].is_number_integer() && env["v"] == 1, "invalid protocol version");
            require(env.contains("t") && env["t"].is_string(), "missing message type");
            const auto type = env["t"].get<std::string>();
            require(type == "hb" || type == "pub" || type == "evt" || type == "res", "unsupported server message type");
            require(env.contains("p") && env["p"].is_object(), "payload must be object");
            require(env.contains("ts") && env["ts"].is_number() && std::isfinite(env["ts"].get<double>()), "invalid timestamp");
            std::string channel, id;
            if (type != "hb") {
                require(env.contains("ch") && env["ch"].is_string(), "missing channel");
                channel = env["ch"].get<std::string>();
                require(validChannel(channel), "invalid channel");
            }
            if (env.contains("robot")) {
                require(env["robot"].is_string() && !env["robot"].get<std::string>().empty(), "invalid robot id");
                const auto robot = env["robot"].get<std::string>();
                require(impl_->robot.empty() || impl_->robot == robot, "E_ROBOT_MISMATCH: robot changed");
                impl_->robot = robot;
            }
            if (type == "res") {
                require(env.contains("id") && env["id"].is_string() && !env["id"].get<std::string>().empty(), "missing response id");
                id = env["id"].get<std::string>();
            }
            Message message{frame.header, frame.payload, type, channel, id, monotonicTime()};
            if (type == "res") {
                const auto reply = response(message);
                const auto pending = impl_->pending.find(id);
                if (pending != impl_->pending.end()) {
                    require(pending->second.channel == channel, "response channel differs from request");
                    if (pending->second.deadline >= Clock::now()) impl_->replies.emplace(id, reply);
                }
            } else if (type == "pub" && channel.rfind("state/", 0) == 0 &&
                       (impl_->states.count(channel) || impl_->states.size() < 128)) {
                impl_->states[channel] = message;
            }
            messages.push_back(std::move(message));
        }
    } catch (const std::exception &e) {
        if (err) *err = e.what();
        impl_->lastStop = Stop::ProtocolError;
        messages.clear(); close(); return false;
    }
    return true;
}
Stop Client::run(const Handler &handler, std::string *err) {
    if (!handler) { if (err) *err = "message handler is required"; return Stop::SocketError; }
    std::vector<Message> messages;
    while (poll(messages, 200, err)) {
        for (const auto &message : messages) {
            if (!handler(message)) { close(); return Stop::Requested; }
            if (!isConnected()) return Stop::Requested;
        }
    }
    return impl_->lastStop;
}
std::string Client::sendRequest(const std::string &channel, const std::string &payloadJson,
                               std::string *err, int timeoutMs) {
    if (err) err->clear();
    impl_->expire();
    if (!validChannel(channel) || timeoutMs <= 0 || impl_->pending.size() >= 128) {
        if (err) *err = "invalid channel/timeout or too many pending requests";
        return {};
    }
    try {
        const auto payload = json::parse(payloadJson);
        require(payload.is_object(), "request payload must be object");
        const auto id = "c" + std::to_string(++impl_->requestSequence);
        if (!impl_->heartbeat(err) || !impl_->send(json{{"v",1},{"t","req"},{"ch",channel},{"id",id},
                                                       {"ts",unixTime()},{"p",payload}}, err)) return {};
        impl_->pending.emplace(id, Impl::Pending{channel, Clock::now() + std::chrono::milliseconds(timeoutMs)});
        return id;
    } catch (const std::exception &e) { if (err) *err = e.what(); return {}; }
}
std::optional<Response> Client::takeResponse(const std::string &id) {
    const auto found = impl_->replies.find(id);
    if (found == impl_->replies.end()) return std::nullopt;
    auto value = found->second;
    forgetRequest(id); return value;
}
void Client::forgetRequest(const std::string &id) { impl_->pending.erase(id); impl_->replies.erase(id); }
std::optional<Response> Client::request(const std::string &channel, const std::string &payloadJson,
                                      int timeoutMs, const Handler &handler, std::string *err) {
    if (impl_->requesting) { if (err) *err = "nested request is unsupported"; return std::nullopt; }
    struct Reset { bool &flag; ~Reset() { flag = false; } } reset{impl_->requesting};
    impl_->requesting = true;
    const auto id = sendRequest(channel, payloadJson, err, timeoutMs);
    if (id.empty()) return std::nullopt;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    try {
        for (;;) {
            if (auto reply = takeResponse(id)) return reply;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (remaining <= 0) { if (err) *err = "request timed out; robot execution is unknown"; break; }
            std::vector<Message> messages;
            if (!poll(messages, static_cast<int>(std::min<std::int64_t>(200, remaining)), err)) break;
            for (const auto &message : messages) {
                if (!(message.type == "res" && message.requestId == id) && handler && !handler(message)) {
                    if (err) *err = "request wait cancelled by handler";
                    forgetRequest(id);
                    return std::nullopt;
                }
            }
        }
    } catch (...) { forgetRequest(id); throw; }
    forgetRequest(id); return std::nullopt;
}
bool Client::publish(const std::string &channel, const std::string &payloadJson, std::string *err) {
    if (err) err->clear();
    if (!validChannel(channel) || channel == "cmd/cmd_vel") { if (err) *err = "invalid channel or disabled TCP velocity"; return false; }
    try {
        const auto payload = json::parse(payloadJson);
        require(payload.is_object(), "publish payload must be object");
        return impl_->heartbeat(err) && impl_->send(json{{"v",1},{"t","pub"},{"ch",channel},{"ts",unixTime()},{"p",payload}}, err);
    } catch (const std::exception &e) { if (err) *err = e.what(); return false; }
}
}  // namespace robot_sdk
