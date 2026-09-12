#include "network/server.hpp"
#include "protocol/resp_serializer.hpp"
#include "common/logger.hpp"

#include <sys/epoll.h>
#include <cerrno>
#include <cstring>
#include <utility>

namespace kv::net {

Server::Server(ServerConfig config)
    : config_(std::move(config)),
      event_loop_(1024),
      storage_(config_.max_memory_bytes),
      aof_(config_.aof_filename, config_.aof_fsync),
      last_cron_run_time_ms_(current_time_ms()) {}

Server::~Server() {
    stop();
}

void Server::start() {
    KV_LOG_INFO("Booting Key-Value Server on {}:{}", config_.bind_address, config_.port);

    // 1. Initialize AOF persistence if enabled
    if (config_.aof_enabled) {
        aof_.open();
    }

    // 2. Initialize TCP listening socket
    listen_socket_ = Socket::create_tcp();
    listen_socket_.set_reuse_addr();
    listen_socket_.set_reuse_port();
    listen_socket_.set_nonblocking();
    listen_socket_.bind(config_.bind_address, config_.port);
    listen_socket_.listen(config_.tcp_backlog);

    // Register listening socket with epoll (nullptr differentiates listener from clients)
    event_loop_.add_fd(listen_socket_.fd(), EPOLLIN, nullptr);

    is_running_ = true;
    KV_LOG_INFO("Server ready to accept connections (Reactor loop running)...");

    // 3. Main Event Loop
    while (is_running_) {
        int num_events = event_loop_.poll(std::chrono::milliseconds(100));

        for (int i = 0; i < num_events; ++i) {
            const auto& event = event_loop_.events()[i];

            if (event.data.ptr == nullptr) {
                // Listen socket readable -> Accept incoming clients
                accept_new_connections();
            } else {
                // Client socket event
                auto* conn = static_cast<Connection*>(event.data.ptr);

                if (event.events & (EPOLLERR | EPOLLHUP)) {
                    conn->handle_close();
                    continue;
                }
                if (event.events & EPOLLIN) {
                    conn->handle_read();
                }
                if (event.events & EPOLLOUT) {
                    conn->handle_write();
                }
            }
        }

        // Run background cron maintenance (active TTL eviction, AOF fsync, idle timeouts)
        run_cron_tasks();
    }

    KV_LOG_INFO("Server event loop stopped.");
}

void Server::stop() {
    if (!is_running_) return;
    is_running_ = false;

    if (config_.aof_enabled) {
        aof_.close();
    }

    connections_.clear();
    listen_socket_.close();
}

void Server::accept_new_connections() {
    while (true) {
        std::string client_ip;
        uint16_t client_port{0};

        int client_fd = listen_socket_.accept(client_ip, client_port);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // SYN queue drained
            } else if (errno == EINTR) {
                continue;
            } else {
                KV_LOG_WARN("accept4() failed: {}", std::strerror(errno));
                break;
            }
        }

        if (connections_.size() >= config_.max_clients) {
            KV_LOG_WARN("maxclients limit reached ({}). Rejecting {}:{}", config_.max_clients, client_ip, client_port);
            ::close(client_fd);
            continue;
        }

        Socket client_sock(client_fd);
        if (config_.tcp_nodelay) {
            client_sock.set_nodelay();
        }

        auto connection = std::make_shared<Connection>(std::move(client_sock), this);
        connections_[client_fd] = connection;

        event_loop_.add_fd(client_fd, EPOLLIN, connection.get());
        KV_LOG_DEBUG("Accepted client {}:{} (fd: {})", client_ip, client_port, client_fd);
    }
}

void Server::update_connection_events(int fd, uint32_t events, void* user_data) {
    event_loop_.modify_fd(fd, events, user_data);
}

void Server::remove_connection(int fd) {
    event_loop_.remove_fd(fd);
    auto it = connections_.find(fd);
    if (it != connections_.end()) {
        KV_LOG_DEBUG("Closed connection on fd {}", fd);
        connections_.erase(it);
    }
}

void Server::process_client_input(Connection* conn, const uint8_t* data, size_t len) {
    size_t cursor = 0;

    // Loop through the buffer to parse and dispatch pipelined commands
    while (cursor < len) {
        size_t consumed = 0;
        Command cmd;

        proto::ParseStatus status = conn->parser().parse(
            data + cursor,
            len - cursor,
            consumed,
            cmd
        );

        cursor += consumed;
        conn->advance_read_offset(consumed);

        if (status == proto::ParseStatus::SUCCESS) {
            dispatch_command(cmd, conn);
        } else if (status == proto::ParseStatus::INCOMPLETE) {
            // Incomplete frame; wait for next EPOLLIN to deliver remainder
            break;
        } else {
            // Protocol error -> send error and close connection
            conn->send_reply(proto::RespSerializer::serialize_error("ERR Protocol error: invalid multibulk length"));
            conn->handle_close();
            return;
        }
    }
}

void Server::dispatch_command(const Command& cmd, Connection* conn) {
    const std::string& name = cmd.name;

    // --- 1. PING [message] ---
    if (name == "PING") {
        if (cmd.args.empty()) {
            conn->send_reply(proto::RespSerializer::serialize_simple_string("PONG"));
        } else {
            conn->send_reply(proto::RespSerializer::serialize_bulk_string(cmd.args[0]));
        }
    }
    // --- 2. ECHO <message> ---
    else if (name == "ECHO") {
        if (cmd.args.empty()) {
            conn->send_reply(proto::RespSerializer::serialize_error("ERR wrong number of arguments for 'echo' command"));
        } else {
            conn->send_reply(proto::RespSerializer::serialize_bulk_string(cmd.args[0]));
        }
    }
    // --- 3. SET key value [EX seconds | PX milliseconds] ---
    else if (name == "SET") {
        if (cmd.args.size() < 2) {
            conn->send_reply(proto::RespSerializer::serialize_error("ERR wrong number of arguments for 'set' command"));
            return;
        }

        const std::string& key = cmd.args[0];
        const std::string& val = cmd.args[1];
        int64_t ttl_ms = -1;

        // Parse optional TTL flags
        for (size_t i = 2; i < cmd.args.size(); ++i) {
            std::string opt = cmd.args[i];
            std::transform(opt.begin(), opt.end(), opt.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });

            if (opt == "EX" && (i + 1) < cmd.args.size()) {
                try {
                    ttl_ms = std::stoll(cmd.args[++i]) * 1000;
                } catch (...) {
                    conn->send_reply(proto::RespSerializer::serialize_error("ERR value is not an integer or out of range"));
                    return;
                }
            } else if (opt == "PX" && (i + 1) < cmd.args.size()) {
                try {
                    ttl_ms = std::stoll(cmd.args[++i]);
                } catch (...) {
                    conn->send_reply(proto::RespSerializer::serialize_error("ERR value is not an integer or out of range"));
                    return;
                }
            }
        }

        StatusCode code = storage_.set(key, val, ttl_ms);
        if (code == StatusCode::OK) {
            // Append canonical form to AOF with absolute timestamp
            if (config_.aof_enabled) {
                if (ttl_ms > 0) {
                    int64_t expire_at = current_time_ms() + ttl_ms;
                    std::vector<std::string> aof_args = {
                        "SET", key, val, "PEXPIREAT", std::to_string(expire_at)
                    };
                    aof_.append(proto::RespSerializer::serialize_array(aof_args));
                } else {
                    std::vector<std::string> aof_args = {"SET", key, val};
                    aof_.append(proto::RespSerializer::serialize_array(aof_args));
                }
            }
            conn->send_reply(proto::RespSerializer::serialize_simple_string("OK"));
        } else if (code == StatusCode::ERR_OOM) {
            conn->send_reply(proto::RespSerializer::serialize_error("OOM command not allowed when used memory > 'maxmemory'"));
        } else {
            conn->send_reply(proto::RespSerializer::serialize_error("ERR failed to set key"));
        }
    }
    // --- 4. GET key ---
    else if (name == "GET") {
        if (cmd.args.size() != 1) {
            conn->send_reply(proto::RespSerializer::serialize_error("ERR wrong number of arguments for 'get' command"));
            return;
        }

        auto result = storage_.get(cmd.args[0]);
        if (result.has_value()) {
            conn->send_reply(proto::RespSerializer::serialize_bulk_string(result.value()));
        } else {
            conn->send_reply(proto::RespSerializer::serialize_null());
        }
    }
    // --- 5. DEL key [key ...] ---
    else if (name == "DEL") {
        if (cmd.args.empty()) {
            conn->send_reply(proto::RespSerializer::serialize_error("ERR wrong number of arguments for 'del' command"));
            return;
        }

        int64_t deleted_count = 0;
        std::vector<std::string> deleted_keys;

        for (const auto& key : cmd.args) {
            if (storage_.del(key)) {
                deleted_count++;
                if (config_.aof_enabled) {
                    deleted_keys.push_back(key);
                }
            }
        }

        if (config_.aof_enabled && !deleted_keys.empty()) {
            std::vector<std::string> aof_args = {"DEL"};
            aof_args.insert(aof_args.end(), deleted_keys.begin(), deleted_keys.end());
            aof_.append(proto::RespSerializer::serialize_array(aof_args));
        }

        conn->send_reply(proto::RespSerializer::serialize_integer(deleted_count));
    }
    // --- 6. EXISTS key [key ...] ---
    else if (name == "EXISTS") {
        if (cmd.args.empty()) {
            conn->send_reply(proto::RespSerializer::serialize_error("ERR wrong number of arguments for 'exists' command"));
            return;
        }

        int64_t count = 0;
        for (const auto& key : cmd.args) {
            if (storage_.exists(key)) count++;
        }
        conn->send_reply(proto::RespSerializer::serialize_integer(count));
    }
    // --- 7. TTL key ---
    else if (name == "TTL") {
        if (cmd.args.size() != 1) {
            conn->send_reply(proto::RespSerializer::serialize_error("ERR wrong number of arguments for 'ttl' command"));
            return;
        }

        int64_t remaining = storage_.ttl(cmd.args[0]);
        conn->send_reply(proto::RespSerializer::serialize_integer(remaining));
    }
    // --- 8. DBSIZE ---
    else if (name == "DBSIZE") {
        conn->send_reply(proto::RespSerializer::serialize_integer(static_cast<int64_t>(storage_.key_count())));
    }
    // --- 9. FLUSHDB ---
    else if (name == "FLUSHDB") {
        storage_.flushdb();
        if (config_.aof_enabled) {
            aof_.append(proto::RespSerializer::serialize_array({"FLUSHDB"}));
        }
        conn->send_reply(proto::RespSerializer::serialize_simple_string("OK"));
    }
    // --- 10. COMMAND (Handles redis-cli automated handshake) ---
    else if (name == "COMMAND") {
        // Return empty array to satisfy redis-cli client initialization
        conn->send_reply("*0\r\n");
    }
    // --- Unknown Command ---
    else {
        conn->send_reply(proto::RespSerializer::serialize_error("ERR unknown command '" + name + "'"));
    }
}

void Server::run_cron_tasks() {
    int64_t now = current_time_ms();
    if ((now - last_cron_run_time_ms_) < 100) return;
    last_cron_run_time_ms_ = now;

    // 1. Active TTL probabilistic eviction cycle (25ms budget)
    storage_.active_expire_cycle(20, 25);

    // 2. Periodic AOF flush/sync
    if (config_.aof_enabled) {
        aof_.check_cron_sync();
    }

    // 3. Client idle timeout checks
    if (config_.client_timeout_sec > 0) {
        int64_t timeout_ms = config_.client_timeout_sec * 1000;
        for (auto it = connections_.begin(); it != connections_.end();) {
            auto& conn = it->second;
            if ((now - conn->last_active_time_ms()) > timeout_ms) {
                KV_LOG_INFO("Closing client fd {} due to idle timeout", it->first);
                event_loop_.remove_fd(it->first);
                it = connections_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

} // namespace kv::net
