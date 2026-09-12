#include "common/logger.hpp"
#include "network/server.hpp"
#include "common/types.hpp"

#include <CLI/CLI.hpp>
#include <csignal>
#include <functional>
#include <iostream>
#include <sys/resource.h>

static std::function<void()> g_shutdown_handler;

void handle_signal(int /*sig*/) {
    if (g_shutdown_handler) {
        g_shutdown_handler();
    }
}

void maximize_file_descriptor_limits() {
    struct rlimit rl{};
    if (::getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max; // Elevate soft limit to hard limit (e.g. 65536)
        if (::setrlimit(RLIMIT_NOFILE, &rl) < 0) {
            // If elevating to max fails, attempt setting to 65536
            rl.rlim_cur = 65536;
            rl.rlim_max = 65536;
            ::setrlimit(RLIMIT_NOFILE, &rl);
        }
    }
}

int main(int argc, char** argv) {
    kv::ServerConfig config;
    std::string fsync_policy_str = "everysec";

    CLI::App app{"High-Performance In-Memory Key-Value Store (Redis Clone)"};

    app.add_option("-p,--port", config.port, "TCP port to listen on")->default_val(6379);
    app.add_option("-b,--bind", config.bind_address, "IP address to bind to")->default_val("0.0.0.0");
    app.add_option("-m,--maxmemory", config.max_memory_bytes, "Maximum memory capacity in bytes")->default_val(256 * 1024 * 1024);
    app.add_option("-c,--maxclients", config.max_clients, "Maximum simultaneous connections")->default_val(10000);
    app.add_option("-l,--loglevel", config.log_level, "Log level threshold (trace, debug, info, warn, err)")->default_val("info");

    app.add_flag("--aof", config.aof_enabled, "Enable Append-Only File (AOF) persistence")->default_val(true);
    app.add_option("--aof-file", config.aof_filename, "AOF log filename")->default_val("appendonly.aof");
    app.add_option("--fsync", fsync_policy_str, "AOF fsync policy: 'always', 'everysec', or 'no'")->default_val("everysec");

    CLI11_PARSE(app, argc, argv);

    // Map fsync policy string to enum
    config.aof_fsync = kv::fsync_policy_from_string(fsync_policy_str);

    // 1. Initialize logging subsystem
    kv::Logger::init(config.log_level);
    KV_LOG_INFO("==================================================================");
    KV_LOG_INFO("  High-Performance C++ Key-Value Store (Redis Clone) v1.0.0       ");
    KV_LOG_INFO("  Architecture: Single-Threaded Reactor (Linux epoll)             ");
    KV_LOG_INFO("==================================================================");

    // 2. Maximize OS file descriptor capacity
    maximize_file_descriptor_limits();

    // 3. Ignore SIGPIPE to avoid abnormal termination on broken client sockets
    std::signal(SIGPIPE, SIG_IGN);

    // 4. Construct server instance
    kv::net::Server server(config);

    // 5. Replay existing AOF write-ahead log to reconstruct memory state
    if (config.aof_enabled) {
        if (!server.aof().replay(server.storage())) {
            KV_LOG_ERROR("Fatal: Failed to replay AOF file. Halting server.");
            return EXIT_FAILURE;
        }
    }

    // 6. Hook OS termination signals for clean, graceful shutdown
    g_shutdown_handler = [&]() {
        KV_LOG_INFO("Shutdown signal received. Flashing dirty buffers to disk...");
        server.stop();
    };

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // 7. Start Reactor event loop (blocks until stopped)
    try {
        server.start();
    } catch (const std::exception& ex) {
        KV_LOG_CRITICAL("Fatal unhandled exception in server loop: {}", ex.what());
        return EXIT_FAILURE;
    }

    kv::Logger::shutdown();
    return EXIT_SUCCESS;
}
