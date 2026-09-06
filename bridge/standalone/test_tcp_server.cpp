// Transport layer tests for the bridge.
//
// Sockets, a background thread and incremental framing meet here, which makes
// this the part of the bridge most likely to be subtly wrong. It is also the
// part that can be tested without ROS 2, so it is.
//
// Deliberately written with plain asserts and no test framework: this has to
// build on a developer laptop, on the robot, and in whatever CI eventually
// exists, without anyone installing anything first.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "shalom/framing.hpp"
#include "shalom_bridge/tcp_server.hpp"

using namespace shalom_bridge;
using namespace std::chrono_literals;

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool condition, const std::string &what)
{
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::cerr << "  FAIL: " << what << "\n";
    }
}

/// 조건이 참이 될 때까지 기다린다. 고정 sleep 보다 빠르고, 느린 장비에서도
/// 흔들리지 않는다.
template <typename Predicate>
bool waitFor(Predicate pred, std::chrono::milliseconds timeout = 2000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred())
            return true;
        std::this_thread::sleep_for(5ms);
    }
    return pred();
}

/// 테스트용 클라이언트 소켓.
class Client {
public:
    bool connect(std::uint16_t port)
    {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0)
            return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        return ::connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
    }

    void write(const std::string &bytes)
    {
        if (fd_ >= 0)
            [[maybe_unused]] auto n = ::write(fd_, bytes.data(), bytes.size());
    }

    std::string readSome(std::chrono::milliseconds timeout = 1000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::string out;
        char buf[4096];
        while (std::chrono::steady_clock::now() < deadline) {
            const ssize_t got = ::recv(fd_, buf, sizeof(buf), MSG_DONTWAIT);
            if (got > 0) {
                out.append(buf, std::size_t(got));
                break;
            }
            std::this_thread::sleep_for(5ms);
        }
        return out;
    }

    void close()
    {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    ~Client() { close(); }

private:
    int fd_ = -1;
};

/// 테스트마다 다른 포트를 써서, 앞선 테스트의 소켓이 남아 있어도 충돌하지 않게 한다.
std::uint16_t nextPort()
{
    static std::uint16_t port = 45210;
    return port++;
}

void run(const char *name, void (*body)(std::uint16_t))
{
    std::cout << "  " << name << "\n";
    body(nextPort());
}

// ============================ 테스트 ============================

void acceptsClientAndReportsConnection(std::uint16_t port)
{
    TcpServer server;
    std::string err;
    check(server.start(port, &err), "start: " + err);

    Client client;
    check(client.connect(port), "client connects");
    check(waitFor([&] { return server.isConnected(); }), "server reports connected");

    bool sawConnect = false;
    waitFor([&] {
        const auto ev = server.drain();
        if (ev.clientConnected)
            sawConnect = true;
        return sawConnect;
    });
    check(sawConnect, "connection event delivered");
    server.stop();
}

void receivesCompleteFrame(std::uint16_t port)
{
    TcpServer server;
    std::string err;
    check(server.start(port, &err), "start: " + err);

    Client client;
    check(client.connect(port), "client connects");
    waitFor([&] { return server.isConnected(); });

    client.write(shalom::encodeFrame(R"({"v":1,"t":"hb"})", "BODY"));

    shalom::Frame got;
    const bool received = waitFor([&] {
        const auto ev = server.drain();
        if (ev.frames.empty())
            return false;
        got = ev.frames.front();
        return true;
    });
    check(received, "frame received");
    check(got.header == R"({"v":1,"t":"hb"})", "header intact");
    check(got.payload == "BODY", "payload intact");
    server.stop();
}

/// 한 프레임이 여러 번에 나뉘어 도착하는 것은 TCP 에서 정상이다.
void reassemblesSplitFrame(std::uint16_t port)
{
    TcpServer server;
    std::string err;
    check(server.start(port, &err), "start: " + err);

    Client client;
    client.connect(port);
    waitFor([&] { return server.isConnected(); });

    const std::string wire = shalom::encodeFrame(R"({"t":"req","ch":"cmd/goto"})");
    for (std::size_t i = 0; i < wire.size(); ++i) {
        client.write(wire.substr(i, 1));
        std::this_thread::sleep_for(1ms);
    }

    bool received = false;
    waitFor([&] {
        const auto ev = server.drain();
        if (!ev.frames.empty())
            received = true;
        return received;
    });
    check(received, "split frame reassembled");
    server.stop();
}

/// 한 번의 쓰기에 여러 프레임이 담겨 온다.
void handlesCoalescedFrames(std::uint16_t port)
{
    TcpServer server;
    std::string err;
    check(server.start(port, &err), "start: " + err);

    Client client;
    client.connect(port);
    waitFor([&] { return server.isConnected(); });

    client.write(shalom::encodeFrame(R"({"n":1})") + shalom::encodeFrame(R"({"n":2})")
                 + shalom::encodeFrame(R"({"n":3})"));

    std::size_t total = 0;
    waitFor([&] {
        total += server.drain().frames.size();
        return total >= 3;
    });
    check(total == 3, "three frames from one write, got " + std::to_string(total));
    server.stop();
}

/// 정합이 깨지면 재동기화하지 않고 끊는다.
void rejectsCorruptedStream(std::uint16_t port)
{
    TcpServer server;
    std::string err;
    check(server.start(port, &err), "start: " + err);

    Client client;
    client.connect(port);
    waitFor([&] { return server.isConnected(); });

    client.write(std::string("\xDE\xAD\xBE\xEF\x08\x00\x00\x00garbage", 20));

    bool sawError = false;
    waitFor([&] {
        const auto ev = server.drain();
        if (ev.protocolError)
            sawError = true;
        return sawError;
    });
    check(sawError, "protocol error reported");
    check(waitFor([&] { return !server.isConnected(); }), "connection closed");
    server.stop();
}

/// 두 번째 관제는 붙지 못한다. 명령 중재 규칙이 없기 때문이다.
void refusesSecondClient(std::uint16_t port)
{
    TcpServer server;
    std::string err;
    check(server.start(port, &err), "start: " + err);

    Client first;
    first.connect(port);
    waitFor([&] { return server.isConnected(); });
    server.drain();

    Client second;
    second.connect(port);   // TCP 로는 붙지만 즉시 닫힌다
    std::this_thread::sleep_for(200ms);

    // 첫 연결은 살아 있어야 한다.
    check(server.isConnected(), "first client remains connected");
    first.write(shalom::encodeFrame(R"({"t":"hb"})"));
    bool stillWorks = false;
    waitFor([&] {
        if (!server.drain().frames.empty())
            stillWorks = true;
        return stillWorks;
    });
    check(stillWorks, "first client still usable after second was refused");
    server.stop();
}

void sendsFramesToClient(std::uint16_t port)
{
    TcpServer server;
    std::string err;
    check(server.start(port, &err), "start: " + err);

    Client client;
    client.connect(port);
    waitFor([&] { return server.isConnected(); });

    server.send(shalom::encodeFrame(R"({"t":"pub","ch":"state/pose"})", "XY"));

    const std::string raw = client.readSome();
    check(!raw.empty(), "client received bytes");

    shalom::FrameDecoder decoder;
    decoder.append(raw);
    shalom::Frame frame;
    check(decoder.next(frame) == shalom::DecodeStatus::Ok, "client decoded a frame");
    check(frame.payload == "XY", "payload round-trips");
    server.stop();
}

void reportsDisconnect(std::uint16_t port)
{
    TcpServer server;
    std::string err;
    check(server.start(port, &err), "start: " + err);

    {
        Client client;
        client.connect(port);
        waitFor([&] { return server.isConnected(); });
        server.drain();
    }   // 클라이언트 소멸 → 소켓 종료

    bool sawDisconnect = false;
    waitFor([&] {
        const auto ev = server.drain();
        if (ev.clientDisconnected)
            sawDisconnect = true;
        return sawDisconnect;
    });
    check(sawDisconnect, "disconnect reported");
    check(!server.isConnected(), "no longer connected");
    server.stop();
}

/// 재연결 시 이전 연결의 잔여 바이트가 남아 있으면 첫 프레임부터 어긋난다.
void resetsDecoderBetweenConnections(std::uint16_t port)
{
    TcpServer server;
    std::string err;
    check(server.start(port, &err), "start: " + err);

    {
        Client first;
        first.connect(port);
        waitFor([&] { return server.isConnected(); });
        // 완결되지 않은 프레임을 남기고 끊는다.
        const std::string partial = shalom::encodeFrame(R"({"t":"hb"})").substr(0, 6);
        first.write(partial);
        std::this_thread::sleep_for(100ms);
    }
    waitFor([&] { return !server.isConnected(); });
    server.drain();

    Client second;
    second.connect(port);
    waitFor([&] { return server.isConnected(); });
    second.write(shalom::encodeFrame(R"({"t":"hb","ch":"fresh"})"));

    shalom::Frame got;
    const bool ok = waitFor([&] {
        const auto ev = server.drain();
        if (ev.frames.empty())
            return false;
        got = ev.frames.front();
        return true;
    });
    check(ok, "new connection delivers a frame");
    check(got.header.find("fresh") != std::string::npos,
          "leftover bytes from the old connection did not corrupt it");
    server.stop();
}

void stopIsPromptAndIdempotent(std::uint16_t port)
{
    TcpServer server;
    std::string err;
    check(server.start(port, &err), "start: " + err);

    const auto began = std::chrono::steady_clock::now();
    server.stop();
    const auto elapsed = std::chrono::steady_clock::now() - began;
    // 자기 파이프로 poll 을 깨우므로 타임아웃을 기다리지 않아야 한다.
    check(elapsed < 500ms, "stop returns promptly");
    server.stop();   // 두 번 불러도 안전해야 한다
    check(true, "stop is idempotent");
}

}  // namespace

int main()
{
    std::cout << "shalom_bridge transport tests\n";

    run("acceptsClientAndReportsConnection", acceptsClientAndReportsConnection);
    run("receivesCompleteFrame", receivesCompleteFrame);
    run("reassemblesSplitFrame", reassemblesSplitFrame);
    run("handlesCoalescedFrames", handlesCoalescedFrames);
    run("rejectsCorruptedStream", rejectsCorruptedStream);
    run("refusesSecondClient", refusesSecondClient);
    run("sendsFramesToClient", sendsFramesToClient);
    run("reportsDisconnect", reportsDisconnect);
    run("resetsDecoderBetweenConnections", resetsDecoderBetweenConnections);
    run("stopIsPromptAndIdempotent", stopIsPromptAndIdempotent);

    std::cout << (gFailures == 0 ? "PASS" : "FAIL") << ": " << (gChecks - gFailures)
              << "/" << gChecks << " checks\n";
    return gFailures == 0 ? 0 : 1;
}
