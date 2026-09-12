#pragma once

#include <sys/epoll.h>
#include <vector>
#include <chrono>
#include <cstdint>

namespace kv::net {
    class EventLoop {
    public:
      explicit EventLoop(int max_events = 1024);
      ~EventLoop();

      EventLoop(const EventLoop&) = delete;
      EventLoop& operator=(const EventLoop&) = delete;

      void add_fd(int fd, uint32_t events, void* user_data);

      void modify_fd(int fd, uint32_t events, void* user_data);

      void remove_fd(int fd);

      int poll(std::chrono::milliseconds timeout);

      [[nodiscard]] const struct epoll_event* events() const noexcept {
        return ready_events_.data();
      }

    private:
      int epoll_fd_{-1};
      std::vector<struct epoll_event> ready_events_;
    };
    
}