// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary
#include <robot_sdk/client.hpp>
#include <cstdio>
#include <string>

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) { std::fprintf(stderr, "Usage: robot_monitor <host> [port]\n"); return 2; }
    int port = 9090;
    try {
        if (argc == 3) { std::size_t used; port = std::stoi(argv[2], &used); if (used != std::string(argv[2]).size()) return 2; }
    } catch (...) { return 2; }
    if (port < 1 || port > 65535) return 2;
    robot_sdk::Client client;
    std::string error;
    if (!client.connect(argv[1], static_cast<std::uint16_t>(port), &error)) {
        std::fprintf(stderr, "%s\n", error.c_str()); return 1;
    }
    const auto stop = client.run([](const robot_sdk::Message &message) {
        if (message.type != "hb") std::printf("%s [%zu bytes] %s\n", message.channel.c_str(),
                                             message.payload.size(), message.envelope.c_str());
        return true;
    }, &error);
    if (stop == robot_sdk::Stop::Requested) return 0;
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
}
