#pragma once

#include "network/socket.hpp"
#include "network/event_loop.hpp"
#include "network/connection.hpp"
#include "storage/engine.hpp"
#include "persistence/aof.hpp"
#include "common/types.hpp"

#include <unordered_map>
#include <memory>
#include <atomic>
#include <string>

namespace kv::net {

/**
 * @brief Central TCP Reactor and connection coordinator.
 * Owns the epoll event loop, storage engine, and AOF logger.
 */
class Server {
public:
    explicit Server(ServerConfig config);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Initializes listener, registers in epoll, and runs reactor loop.
    void start();

    /// Signals the reactor event loop to terminate gracefully.
    void stop();

    void update_connection_events(int fd, uint32_t events, void* user_data);
    void remove_connection(int fd);

    /// Routes incoming bytes through client's RespParser and dispatches commands
    void process_client_input(Connection* conn, const uint8_t* data, size_t len);

    /// Executes recognized Redis commands against StorageEngine & AofLogger
    void dispatch_command(const Command& cmd, Connection* conn);

    [[nodiscard]] const ServerConfig& config() const noexcept { return config_; }
    [[nodiscard]] storage::Engine& storage() noexcept { return storage_; }
    [[nodiscard]] persist::AofLogger& aof() noexcept { return aof_; }

private:
    void accept_new_connections();
    void run_cron_tasks();

    ServerConfig config_;
    std::atomic<bool> is_running_{false};

    Socket listen_socket_;
    EventLoop event_loop_;

    std::unordered_map<int, std::shared_ptr<Connection>> connections_;

    // Core Subsystem Instances
    storage::Engine storage_;
    persist::AofLogger aof_;

    int64_t last_cron_run_time_ms_{0};
};

} // namespace kv::net
