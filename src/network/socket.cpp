#include "network/socket.hpp"
#include "common/logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace kv::net {

Socket::Socket(int fd) noexcept : fd_(fd) {}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void Socket::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

Socket Socket::create_tcp() {
    // SOCK_CLOEXEC ensures descriptor is closed automatically if fork/exec is called.
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        KV_LOG_ERROR("Failed to create TCP socket: {}", std::strerror(errno));
        throw std::runtime_error("socket() system call failed");
    }
    return Socket(fd);
}

void Socket::set_nonblocking() const {
    int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        KV_LOG_ERROR("fcntl(O_NONBLOCK) failed on fd {}: {}", fd_, std::strerror(errno));
        throw std::runtime_error("Failed to set non-blocking flag on socket");
    }
}

void Socket::set_reuse_addr() const {
    int opt = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        KV_LOG_WARN("setsockopt(SO_REUSEADDR) failed on fd {}: {}", fd_, std::strerror(errno));
    }
}

void Socket::set_reuse_port() const {
    int opt = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        KV_LOG_WARN("setsockopt(SO_REUSEPORT) failed on fd {}: {}", fd_, std::strerror(errno));
    }
}

void Socket::set_nodelay() const {
    int opt = 1;
    if (::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        KV_LOG_WARN("setsockopt(TCP_NODELAY) failed on fd {}: {}", fd_, std::strerror(errno));
    }
}

void Socket::bind(const std::string& ip, uint16_t port) const {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (ip.empty() || ip == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            KV_LOG_ERROR("Invalid IPv4 bind address: {}", ip);
            throw std::invalid_argument("Invalid IP address");
        }
    }

    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        KV_LOG_ERROR("bind() failed on {}:{}: {}", ip, port, std::strerror(errno));
        throw std::runtime_error("bind() failed");
    }
}

void Socket::listen(int backlog) const {
    if (::listen(fd_, backlog) < 0) {
        KV_LOG_ERROR("listen() failed on fd {}: {}", fd_, std::strerror(errno));
        throw std::runtime_error("listen() failed");
    }
}

int Socket::accept(std::string& client_ip, uint16_t& client_port) const {
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    // Accept with SOCK_NONBLOCK and SOCK_CLOEXEC atomically
    int client_fd = ::accept4(fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd >= 0) {
        char ip_str[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        client_ip = ip_str;
        client_port = ntohs(client_addr.sin_port);
    }
    return client_fd;
}

} // namespace kv::net
