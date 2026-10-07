// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary
#include <robot_sdk/api.hpp>
#include <cstdio>
#include <string>
#include <chrono>

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) { std::fprintf(stderr, "Usage: robot_api_example <host> [port]\n"); return 2; }
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
    // Read-only query: responses and state publications can arrive in either order.
    auto reply = client.request("cmd/maps/list", "{}", 3000, {}, &error);
    if (!reply) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    if (!reply->ok) { std::fprintf(stderr, "%s: %s\n", reply->errorCode.c_str(), reply->errorMessage.c_str()); return 1; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!client.latest("state/maps") && std::chrono::steady_clock::now() < deadline) {
        std::vector<robot_sdk::Message> messages;
        if (!client.poll(messages, 200, &error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    }
    auto maps = client.latest("state/maps");
    if (!maps) { std::fprintf(stderr, "map list was not received\n"); return 1; }
    std::printf("%s\n", maps->envelope.c_str());
    return 0;
}
