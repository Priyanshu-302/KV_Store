#include "network/connection.hpp"
#include "network/server.hpp"
#include "common/logger.hpp"

#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace kv::net {

// Initial read buffer capacity: 8KB page
constexpr size_t INITIAL_BUFFER_SIZE = 8192;
// Maximum allowed client read buffer size to prevent memory DOS (32MB)
constexpr size_t MAX_READ_BUFFER_SIZE = 32 * 1024 * 1024;

Connection::Connection(Socket socket, Server* server)
    : socket_(std::move(socket)),
      server_(server),
      read_buffer_(INITIAL_BUFFER_SIZE),
      last_active_time_ms_(current_time_ms()) {}

void Connection::compact_read_buffer() {
    if (read_offset_ > 0) {
        size_t unparsed_bytes = write_offset_rx_ - read_offset_;
        if (unparsed_bytes > 0) {
            std::memmove(read_buffer_.data(), read_buffer_.data() + read_offset_, unparsed_bytes);
        }
        write_offset_rx_ = unparsed_bytes;
        read_offset_ = 0;
    }
}

void Connection::handle_read() {
    last_active_time_ms_ = current_time_ms();

    // Draining loop: read until EAGAIN / EWOULDBLOCK
    while (true) {
        // Expand buffer if nearly full
        if (write_offset_rx_ == read_buffer_.size()) {
            if (read_buffer_.size() >= MAX_READ_BUFFER_SIZE) {
                KV_LOG_WARN("Client fd {} breached max read buffer limit. Closing.", fd());
                handle_close();
                return;
            }
            read_buffer_.resize(read_buffer_.size() * 2);
        }

        uint8_t* dest = read_buffer_.data() + write_offset_rx_;
        size_t available_capacity = read_buffer_.size() - write_offset_rx_;

        ssize_t bytes_read = ::read(fd(), dest, available_capacity);

        if (bytes_read > 0) {
            write_offset_rx_ += static_cast<size_t>(bytes_read);
            
            // Delegate byte stream to the server command parsing engine
            server_->process_client_input(this, read_buffer_.data() + read_offset_, write_offset_rx_ - read_offset_);
        } else if (bytes_read == 0) {
            // Client cleanly closed connection (FIN packet)
            KV_LOG_DEBUG("Client fd {} disconnected cleanly (EOF)", fd());
            handle_close();
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Kernel RX buffer completely drained for this cycle
                break;
            } else if (errno == EINTR) {
                continue; // Interrupted by signal, retry read immediately
            } else {
                KV_LOG_ERROR("read() error on client fd {}: {}", fd(), std::strerror(errno));
                handle_close();
                return;
            }
        }
    }

    // Shift unparsed data if offset has progressed significantly
    if (read_offset_ > (read_buffer_.size() / 2)) {
        compact_read_buffer();
    }
}

void Connection::send_reply(std::string_view reply) {
    if (reply.empty()) return;

    // Enforce backpressure limits
    const auto& config = server_->config();
    size_t pending = (write_buffer_.size() - write_offset_) + reply.size();

    // 1. Hard limit check
    if (pending > config.client_output_buffer_limit_hard) {
        KV_LOG_WARN("Client fd {} breached hard write buffer limit ({} bytes). Dropping.", fd(), pending);
        handle_close();
        return;
    }

    // 2. Soft limit check
    if (pending > config.client_output_buffer_limit_soft) {
        int64_t now = current_time_ms();
        if (soft_limit_breach_time_ms_ == 0) {
            soft_limit_breach_time_ms_ = now;
        } else if ((now - soft_limit_breach_time_ms_) > (config.client_output_buffer_soft_seconds * 1000)) {
            KV_LOG_WARN("Client fd {} stayed above soft write buffer limit for >10s. Dropping.", fd());
            handle_close();
            return;
        }
    } else {
        soft_limit_breach_time_ms_ = 0; // Reset soft limit timer
    }

    // Append reply to write buffer
    write_buffer_.insert(write_buffer_.end(), reply.begin(), reply.end());

    // If no prior writes were pending, attempt immediate flush
    if (write_offset_ == 0 || write_offset_ == (write_buffer_.size() - reply.size())) {
        flush_write_buffer();
    }
}

bool Connection::flush_write_buffer() {
    while (write_offset_ < write_buffer_.size()) {
        const uint8_t* src = write_buffer_.data() + write_offset_;
        size_t remaining = write_buffer_.size() - write_offset_;

        // MSG_NOSIGNAL prevents SIGPIPE when client abruptly disconnects
        ssize_t sent = ::send(fd(), src, remaining, MSG_NOSIGNAL);

        if (sent > 0) {
            write_offset_ += static_cast<size_t>(sent);
        } else if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Kernel TX buffer is full; register EPOLLOUT to continue when writable
                server_->update_connection_events(fd(), EPOLLIN | EPOLLOUT, this);
                return false;
            } else if (errno == EINTR) {
                continue;
            } else {
                KV_LOG_DEBUG("Write error on fd {}: {}", fd(), std::strerror(errno));
                handle_close();
                return false;
            }
        }
    }

    // Entire write buffer transmitted successfully
    write_buffer_.clear();
    write_offset_ = 0;

    // Deregister EPOLLOUT interest so we don't busy-spin on Level-Triggered mode
    server_->update_connection_events(fd(), EPOLLIN, this);
    return true;
}

void Connection::handle_write() {
    last_active_time_ms_ = current_time_ms();
    flush_write_buffer();
}

void Connection::handle_close() {
    server_->remove_connection(fd());
}

} // namespace kv::net
