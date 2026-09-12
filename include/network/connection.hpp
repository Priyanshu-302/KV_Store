#pragma once

#include "network/socket.hpp"
#include "protocol/resp_parser.hpp"
#include "common/types.hpp"

#include <vector>
#include <string_view>
#include <memory>
#include <cstdint>

namespace kv::net {

class Server;

/**
 * @brief Manages client connection state, dynamic read/write byte buffers,
 * and individual streaming parser state.
 */
class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(Socket socket, Server* server);
    ~Connection() = default;

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    [[nodiscard]] int fd() const noexcept { return socket_.fd(); }

    /// Handles incoming socket readability (EPOLLIN)
    void handle_read();

    /// Handles socket writability (EPOLLOUT) to flush buffered responses
    void handle_write();

    /// Handles socket closure and session cleanup
    void handle_close();

    /// Appends response bytes and flushes non-blocking
    void send_reply(std::string_view reply);

    /// Advances the read offset after consuming parsed tokens
    void advance_read_offset(size_t consumed) noexcept {
        read_offset_ += consumed;
    }

    [[nodiscard]] proto::RespParser& parser() noexcept { return parser_; }

    [[nodiscard]] bool has_pending_writes() const noexcept {
        return write_offset_ < write_buffer_.size();
    }

    [[nodiscard]] int64_t last_active_time_ms() const noexcept {
        return last_active_time_ms_;
    }

private:
    bool flush_write_buffer();
    void compact_read_buffer();

    Socket socket_;
    Server* server_{nullptr};

    // Streaming Read Buffer
    std::vector<uint8_t> read_buffer_;
    size_t read_offset_{0};
    size_t write_offset_rx_{0};

    // Streaming Write Buffer
    std::vector<uint8_t> write_buffer_;
    size_t write_offset_{0};

    // Per-client streaming RESP parser (preserves state across TCP splits)
    proto::RespParser parser_;

    int64_t last_active_time_ms_{0};
    int64_t soft_limit_breach_time_ms_{0};
};

} // namespace kv::net
