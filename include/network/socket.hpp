#pragma once

#include <string>
#include <cstdint>

namespace kv::net {
    class Socket {
    public:
      explicit Socket(int fd = -1) noexcept;
      ~Socket();

      Socket(const Socket&) = delete;
      Socket& operator=(const Socket&) = delete;

      Socket(Socket&& other) noexcept;
      Socket& operator=(Socket&& other) noexcept;

      static Socket create_tcp();

      void close() noexcept;

      [[nodiscard]] int fd() const noexcept { return fd_; }

      void set_nonblocking() const;

      void set_reuse_addr() const;

      void set_reuse_port() const;

      void set_nodelay() const;

      void bind(const std::string& ip, uint16_t port) const;

      void listen(int backlog = 1024) const;

      [[nodiscard]] int accept(std::string& client_ip, uint16_t& client_port) const;

    private:
      int fd_{-1};
    };
}