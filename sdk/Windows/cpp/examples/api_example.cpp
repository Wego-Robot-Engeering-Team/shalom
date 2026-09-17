// Copyright (c) 2026 WeGo Robotics. All rights reserved.
//
// Public-header API example for an authorised customer application.
//
// This sample uses only cmd/maps/list, a read-only query. It demonstrates the
// C++ request/response lifecycle without providing copy-and-run motion code.
// See docs/API.md before enabling an operational command in a customer UI.

#include <shalom/api.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

std::string stringField(const std::string &json, const std::string &key)
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
        std::fprintf(stderr, "Usage: shalom_api_example <host> [port]\n");
        return 2;
    }

    const std::string host = argv[1];
    const auto port = static_cast<std::uint16_t>(argc > 2 ? std::atoi(argv[2]) : 9090);

    shalom::Client client;
    std::string error;
    if (!client.connect(host, port, &error)) {
        std::fprintf(stderr, "Connection failed: %s\n", error.c_str());
        return 1;
    }

    // RobotApi is the public command facade supplied in shalom/api.hpp.
    // The returned id correlates this request with the later `res` frame.
    shalom::RobotApi robot(client);
    const std::string requestId = robot.listMaps(&error);
    if (requestId.empty()) {
        std::fprintf(stderr, "Request failed to send: %s\n", error.c_str());
        return 1;
    }

    std::printf("Requested map list (%s); waiting for response...\n", requestId.c_str());
    const auto stop = client.run(
        [&](const shalom::Message &message) {
            const std::string type = stringField(message.envelope, "t");
            const std::string channel = stringField(message.envelope, "ch");

            if (type == "pub" && channel == "state/maps") {
                // Use the JSON library already used by the customer application
                // to parse message.envelope and render the actual map list.
                std::printf("Received state/maps: %s\n", message.envelope.c_str());
                return true;
            }
            if (type == "res" && stringField(message.envelope, "id") == requestId) {
                std::printf("Request response: %s\n", message.envelope.c_str());
                return false;  // Client::run returns Stop::Requested.
            }
            return true;
        },
        &error);

    if (stop == shalom::Stop::Requested)
        return 0;
    std::fprintf(stderr, "Connection ended: %s\n", error.c_str());
    return 1;
}
