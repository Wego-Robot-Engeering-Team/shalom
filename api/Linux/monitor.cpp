// Copyright (c) 2026 WeGo Robotics. All rights reserved.

// Read-only monitor for the GTX-A inspection robot.
//
// Connects, keeps the heartbeat going, and prints one line per frame. Builds
// unchanged on Linux, macOS and Windows.
//
// Sending commands is left out on purpose. Motion, mission, arm and E-Stop
// belong in an application with an operator in front of it and a safety story
// of its own; copy-and-paste sample code is the wrong place to learn them from.
//
//     monitor <host> [port]

#include "shalom/client.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

/// Extracts one top-level string field. See client.hpp for why the SDK does
/// not bring a JSON library along - this is a printer, not a parser.
std::string field(const std::string &json, const std::string &key)
{
    const std::string needle = "\"" + key + "\":\"";
    const auto at = json.find(needle);
    if (at == std::string::npos)
        return {};
    const auto begin = at + needle.size();
    const auto end = json.find('"', begin);
    return end == std::string::npos ? std::string{} : json.substr(begin, end - begin);
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "사용법: monitor <host> [port]\n");
        return 2;
    }
    const std::string host = argv[1];
    const auto port = static_cast<std::uint16_t>(argc > 2 ? std::atoi(argv[2]) : 9090);

    shalom::Client client;
    std::string err;
    if (!client.connect(host, port, &err)) {
        std::fprintf(stderr, "연결 실패: %s\n", err.c_str());
        return 1;
    }
    std::printf("연결됨: %s:%u\n", host.c_str(), unsigned(port));

    long long frames = 0;
    const auto stop = client.run(
        [&](const shalom::Message &msg) {
            const std::string type = field(msg.envelope, "t");
            if (type == "hb")
                return true;   // 5 Hz in both directions; printing it is noise

            const std::string channel = field(msg.envelope, "ch");
            if (msg.payload.empty())
                std::printf("%-22s %s\n", channel.c_str(), msg.envelope.c_str());
            else
                std::printf("%-22s [%zu 바이트] %s\n", channel.c_str(),
                            msg.payload.size(), msg.envelope.c_str());

            // evt/log carries the operator-facing code. Every code is listed in
            // errors.md with its cause and what to do about it.
            if (channel == "evt/log") {
                const std::string code = field(msg.envelope, "code");
                if (!code.empty())
                    std::printf("  >> %s\n", code.c_str());
            }
            return ++frames < 100000;
        },
        &err);

    switch (stop) {
    case shalom::Stop::Requested:
        std::printf("종료: 요청됨\n");
        return 0;
    case shalom::Stop::PeerClosed:
        std::printf("종료: 로봇이 연결을 닫았습니다\n");
        return 0;
    case shalom::Stop::ProtocolError:
        std::fprintf(stderr, "종료: %s\n", err.c_str());
        return 1;
    case shalom::Stop::SocketError:
        std::fprintf(stderr, "종료: %s\n", err.c_str());
        return 1;
    }
    return 0;
}
