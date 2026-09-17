// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "shalom/client.hpp"

#include "transport/framing.hpp"
#include "transport/socket.hpp"

#include <chrono>
#include <cstdio>
#include <utility>

namespace shalom {
namespace {

constexpr auto kHeartbeatPeriod = std::chrono::milliseconds{200};

std::string timestamp()
{
    using clock = std::chrono::system_clock;
    const double seconds = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.3f", seconds);
    return buffer;
}

std::string extractString(const std::string &json, const std::string &key)
{
    const std::string needle = "\"" + key + "\"";
    const auto at = json.find(needle);
    if (at == std::string::npos)
        return {};
    auto index = json.find(':', at + needle.size());
    if (index == std::string::npos)
        return {};
    while (++index < json.size() && (json[index] == ' ' || json[index] == '\t')) {}
    if (index >= json.size() || json[index] != '"')
        return {};
    const auto begin = ++index;
    while (index < json.size() && json[index] != '"') {
        if (json[index] == '\\')
            ++index;
        ++index;
    }
    return index <= json.size() ? json.substr(begin, index - begin) : std::string{};
}

}  // namespace

class Client::Impl {
public:
    TcpClient socket;
    inspection::FrameDecoder decoder;
    std::string robot;
    std::int64_t sequence = 0;
    std::int64_t requestId = 0;

    bool sendHeartbeat(std::string *err)
    {
        const std::string header = "{\"v\":1,\"t\":\"hb\",\"ts\":" + timestamp()
                                   + ",\"p\":{\"seq\":" + std::to_string(++sequence) + "}}";
        return socket.sendAll(inspection::encodeFrame(header), err);
    }

    bool checkRobot(const std::string &envelope, std::string *err)
    {
        const std::string id = extractString(envelope, "robot");
        if (id.empty())
            return true;
        if (robot.empty()) {
            robot = id;
            return true;
        }
        if (robot != id) {
            if (err)
                *err = "다른 로봇이 응답했습니다 (E_ROBOT_MISMATCH): " + robot + " -> " + id;
            return false;
        }
        return true;
    }
};

Client::Client() : impl_(std::make_unique<Impl>()) {}
Client::~Client() = default;
Client::Client(Client &&) noexcept = default;
Client &Client::operator=(Client &&) noexcept = default;

bool Client::connect(const std::string &host, std::uint16_t port, std::string *err)
{
    impl_->decoder.reset();
    impl_->robot.clear();
    impl_->sequence = 0;
    return impl_->socket.connect(host, port, err);
}

void Client::close()
{
    impl_->socket.close();
}

bool Client::isConnected() const
{
    return impl_->socket.isConnected();
}

const std::string &Client::robotId() const
{
    return impl_->robot;
}

Stop Client::run(const Handler &onMessage, std::string *err)
{
    using clock = std::chrono::steady_clock;
    auto nextBeat = clock::now();
    for (;;) {
        const auto now = clock::now();
        if (now >= nextBeat) {
            if (!impl_->sendHeartbeat(err))
                return Stop::SocketError;
            nextBeat = now + kHeartbeatPeriod;
        }

        const auto waitMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(nextBeat - clock::now()).count());
        std::string chunk;
        switch (impl_->socket.read(chunk, waitMs > 0 ? waitMs : 0)) {
        case ReadStatus::Timeout:
            continue;
        case ReadStatus::Closed:
            return Stop::PeerClosed;
        case ReadStatus::Error:
            if (err)
                *err = "수신 실패: " + TcpClient::lastError();
            return Stop::SocketError;
        case ReadStatus::Data:
            break;
        }

        impl_->decoder.append(chunk);
        for (;;) {
            inspection::Frame frame;
            const auto status = impl_->decoder.next(frame);
            if (status == inspection::DecodeStatus::NeedMore)
                break;
            if (status != inspection::DecodeStatus::Ok) {
                if (err) {
                    *err = status == inspection::DecodeStatus::BadMagic
                               ? "프레임 동기가 깨졌습니다 (LINK_FRAME_CORRUPT)"
                               : "프레임 크기가 규격을 벗어났습니다 (LINK_FRAME_TOO_LARGE)";
                }
                return Stop::ProtocolError;
            }
            if (!impl_->checkRobot(frame.header, err))
                return Stop::ProtocolError;
            if (!onMessage(Message{frame.header, frame.payload}))
                return Stop::Requested;
        }
    }
}

std::string Client::sendRequest(const std::string &channel, const std::string &payloadJson,
                                std::string *err)
{
    const std::string id = "c" + std::to_string(++impl_->requestId);
    const std::string header = "{\"v\":1,\"t\":\"req\",\"ch\":\"" + channel
                               + "\",\"id\":\"" + id + "\",\"ts\":" + timestamp()
                               + ",\"p\":" + payloadJson + "}";
    if (!impl_->socket.sendAll(inspection::encodeFrame(header), err))
        return {};
    return id;
}

bool Client::publish(const std::string &channel, const std::string &payloadJson, std::string *err)
{
    const std::string header = "{\"v\":1,\"t\":\"pub\",\"ch\":\"" + channel
                               + "\",\"ts\":" + timestamp() + ",\"p\":" + payloadJson + "}";
    return impl_->socket.sendAll(inspection::encodeFrame(header), err);
}

}  // namespace shalom
