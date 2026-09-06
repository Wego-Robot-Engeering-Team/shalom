#include "shalom_bridge/tcp_server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace shalom_bridge {
namespace {

bool setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}  // namespace

TcpServer::~TcpServer()
{
    stop();
}

bool TcpServer::start(std::uint16_t port, std::string *err)
{
    const auto fail = [err](const std::string &m) {
        if (err)
            *err = m + ": " + std::strerror(errno);
        return false;
    };

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0)
        return fail("socket");

    // 재기동 시 TIME_WAIT 로 바인드가 막히지 않게 한다. 로봇 소프트웨어는
    // 현장에서 자주 재시작되고, 그때마다 몇 분을 기다릴 수는 없다.
    int one = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(listenFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return fail("bind");
    }
    if (::listen(listenFd_, 4) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return fail("listen");
    }
    if (!setNonBlocking(listenFd_)) {
        ::close(listenFd_);
        listenFd_ = -1;
        return fail("fcntl");
    }

    // stop() 이 poll() 을 즉시 깨울 수 있도록 자기 파이프를 둔다. 타임아웃에
    // 기대면 종료가 그만큼 늦어지고, 종료 지연은 재기동 시간에 그대로 붙는다.
    if (::pipe(wakeFd_) != 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return fail("pipe");
    }
    setNonBlocking(wakeFd_[0]);

    running_.store(true);
    thread_ = std::thread([this] { runLoop(); });
    return true;
}

void TcpServer::stop()
{
    if (!running_.exchange(false))
        return;

    if (wakeFd_[1] >= 0) {
        const char b = 1;
        [[maybe_unused]] const auto n = ::write(wakeFd_[1], &b, 1);
    }
    if (thread_.joinable())
        thread_.join();

    closeClient(false);
    for (int *fd : {&listenFd_, &wakeFd_[0], &wakeFd_[1]}) {
        if (*fd >= 0) {
            ::close(*fd);
            *fd = -1;
        }
    }
}

void TcpServer::closeClient(bool notify)
{
    if (clientFd_ >= 0) {
        ::close(clientFd_);
        clientFd_ = -1;
    }
    decoder_.reset();
    {
        std::lock_guard<std::mutex> lock(outMutex_);
        outBuffer_.clear();
    }
    if (connected_.exchange(false) && notify) {
        std::lock_guard<std::mutex> lock(eventMutex_);
        events_.clientDisconnected = true;
    }
}

void TcpServer::send(std::string encodedFrame, bool lossy)
{
    std::lock_guard<std::mutex> lock(outMutex_);
    // 관제가 읽기를 멈춘 경우 손실 허용 메시지는 버린다. 오래된 위치를
    // 뒤늦게 보내는 것은 큐만 키우고 아무 가치가 없다.
    if (lossy && outBuffer_.size() > kMaxOutboundBytes)
        return;
    outBuffer_ += encodedFrame;
}

LinkEvents TcpServer::drain()
{
    std::lock_guard<std::mutex> lock(eventMutex_);
    LinkEvents out = std::move(events_);
    events_ = LinkEvents{};
    return out;
}

void TcpServer::resetByteCounters()
{
    rxBytes_.store(0);
    txBytes_.store(0);
}

void TcpServer::runLoop()
{
    while (running_.load()) {
        pollfd fds[3];
        int n = 0;

        fds[n++] = {wakeFd_[0], POLLIN, 0};
        fds[n++] = {listenFd_, POLLIN, 0};

        bool wantWrite = false;
        {
            std::lock_guard<std::mutex> lock(outMutex_);
            wantWrite = !outBuffer_.empty();
        }
        if (clientFd_ >= 0)
            fds[n++] = {clientFd_, short(POLLIN | (wantWrite ? POLLOUT : 0)), 0};

        // 보낼 것이 있으면 짧게, 없으면 조금 길게 기다린다.
        const int timeoutMs = wantWrite ? 5 : 50;
        if (::poll(fds, nfds_t(n), timeoutMs) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (!running_.load())
            break;

        if (fds[0].revents & POLLIN) {
            char sink[64];
            while (::read(wakeFd_[0], sink, sizeof(sink)) > 0) {
            }
        }

        if (fds[1].revents & POLLIN) {
            const int fd = ::accept(listenFd_, nullptr, nullptr);
            if (fd >= 0) {
                if (clientFd_ >= 0) {
                    // 이미 접속된 관제가 있다. 두 번째는 즉시 닫는다 —
                    // 명령 중재 규칙이 없는 상태에서 두 화면을 붙이면
                    // 어느 쪽이 권위인지 아무도 모른다.
                    ::close(fd);
                } else {
                    setNonBlocking(fd);
                    // Nagle 을 끈다. 하트비트와 응답은 작고 지연에 민감하다.
                    int one = 1;
                    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                    ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
                    clientFd_ = fd;
                    decoder_.reset();
                    connected_.store(true);
                    std::lock_guard<std::mutex> lock(eventMutex_);
                    events_.clientConnected = true;
                }
            }
        }

        if (clientFd_ >= 0 && n >= 3) {
            const short re = fds[2].revents;
            if (re & (POLLHUP | POLLERR | POLLNVAL)) {
                closeClient(true);
                continue;
            }
            if (re & POLLIN)
                handleReadable();
            if (clientFd_ >= 0 && (re & POLLOUT))
                handleWritable();
        }
    }
}

void TcpServer::handleReadable()
{
    char buf[16384];
    for (;;) {
        const ssize_t got = ::read(clientFd_, buf, sizeof(buf));
        if (got > 0) {
            rxBytes_.fetch_add(std::uint64_t(got));
            decoder_.append(buf, std::size_t(got));
            continue;
        }
        if (got == 0) {
            closeClient(true);   // 상대가 정상 종료
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        if (errno == EINTR)
            continue;
        closeClient(true);
        return;
    }

    // 한 번의 읽기에 여러 프레임이 들어온다. 버퍼가 마를 때까지 꺼낸다.
    shalom::Frame frame;
    for (;;) {
        const auto status = decoder_.next(frame);
        if (status == shalom::DecodeStatus::NeedMore)
            return;
        if (status != shalom::DecodeStatus::Ok) {
            // 정합이 깨졌다. 재동기화를 시도하지 않고 끊는다.
            {
                std::lock_guard<std::mutex> lock(eventMutex_);
                events_.protocolError = true;
                events_.protocolErrorDetail =
                    status == shalom::DecodeStatus::BadMagic
                        ? "frame magic mismatch - stream desynchronised"
                        : "declared frame length is impossible";
            }
            closeClient(true);
            return;
        }
        std::lock_guard<std::mutex> lock(eventMutex_);
        events_.frames.push_back(frame);
    }
}

void TcpServer::handleWritable()
{
    std::lock_guard<std::mutex> lock(outMutex_);
    while (!outBuffer_.empty()) {
        const ssize_t sent = ::write(clientFd_, outBuffer_.data(), outBuffer_.size());
        if (sent > 0) {
            txBytes_.fetch_add(std::uint64_t(sent));
            outBuffer_.erase(0, std::size_t(sent));
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;   // 소켓이 찼다. 다음 POLLOUT 에서 이어서 보낸다.
        if (sent < 0 && errno == EINTR)
            continue;
        break;
    }
}

}  // namespace shalom_bridge
