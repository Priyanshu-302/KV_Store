#include "network/event_loop.hpp"
#include "common/logger.hpp"

#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace kv::net {
  EventLoop::EventLoop(int max_events)
    :ready_events_(static_cast<size_t>(max_events)) {
      // prevents event leak
      epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
      if (epoll_fd_ < 0) {
      KV_LOG_ERROR("epoll_create1 failed: {}", std::strerror(errno));
        throw std::runtime_error("Failed to initialize epoll instance");
      }
    }

  EventLoop::~EventLoop() {
    if (epoll_fd_ > 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
  }

  void EventLoop::add_fd(int fd, uint32_t events, void* user_data) {
    struct epoll_event ev{};
    ev.events = events;
    ev.data.ptr = user_data;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        KV_LOG_ERROR("epoll_ctl(EPOLL_CTL_ADD) failed on fd {}: {}", fd, std::strerror(errno));
    }
  }

  void EventLoop::modify_fd(int fd, uint32_t events, void* user_data) {
    struct epoll_event ev{};
    ev.events = events;
    ev.data.ptr = user_data;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        KV_LOG_ERROR("epoll_ctl(EPOLL_CTL_MOD) failed on fd {}: {}", fd, std::strerror(errno));
    }
  }

  void EventLoop::remove_fd(int fd) {
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        if (errno != ENOENT) { // Ignore if already deregistered
            KV_LOG_WARN("epoll_ctl(EPOLL_CTL_DEL) failed on fd {}: {}", fd, std::strerror(errno));
        }
    }
  }

  int EventLoop::poll(std::chrono::milliseconds timeout) {
    int timeout_ms = static_cast<int>(timeout.count());

    int num_events = ::epoll_wait(
        epoll_fd_,
        ready_events_.data(),
        static_cast<int>(ready_events_.size()),
        timeout_ms
    );

    if (num_events < 0) {
        if (errno == EINTR) {
            return 0;
        }
        KV_LOG_ERROR("epoll_wait failed: {}", std::strerror(errno));
        return -1;
    }

    return num_events;
  }

}