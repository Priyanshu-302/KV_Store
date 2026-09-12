#pragma once

#include "common/types.hpp"
#include "storage/engine.hpp"

#include <string>
#include <string_view>
#include <cstdint>

namespace kv::persist {

/**
 * @brief Manages Append-Only File (AOF) durability logging and startup recovery.
 * 
 * Writes state-altering commands to an append-only disk log using low-level POSIX
 * file descriptors and configurable kernel sync intervals (fdatasync).
 */
class AofLogger {
public:
    AofLogger(std::string filename, FsyncPolicy policy);
    ~AofLogger();

    AofLogger(const AofLogger&) = delete;
    AofLogger& operator=(const AofLogger&) = delete;

    /// Opens the AOF file in append-only write mode (O_WRONLY | O_CREAT | O_APPEND).
    void open();

    /// Flushes any pending buffers and safely closes the file descriptor.
    void close() noexcept;

    /**
     * @brief Appends a serialized RESP command into the user-space write buffer.
     * If policy is ALWAYS, immediately flushes to disk and invokes fdatasync().
     */
    void append(std::string_view serialized_resp_cmd);

    /**
     * @brief Flushes pending user-space buffer to kernel page cache via POSIX ::write().
     */
    void flush_buffer();

    /**
     * @brief Synchronously flushes kernel dirty pages to physical disk via ::fdatasync().
     */
    void sync_to_disk();

    /**
     * @brief Periodic check invoked by Server cron. Triggers fdatasync() if
     * policy is EVERYSEC and >= 1 second has elapsed since the last sync.
     */
    void check_cron_sync();

    /**
     * @brief Reconstructs in-memory storage state from disk on server boot.
     * Reads sequential RESP commands and applies them directly into storage::Engine.
     * 
     * @param engine Reference to the initialized storage engine.
     * @return true if recovery succeeded or file did not exist, false on fatal corruption.
     */
    bool replay(storage::Engine& engine);

    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }
    [[nodiscard]] const std::string& filename() const noexcept { return filename_; }

private:
    std::string filename_;
    FsyncPolicy policy_{FsyncPolicy::EVERYSEC};
    int fd_{-1};
    std::string buffer_;              // In-memory write buffer before ::write() flush
    int64_t last_sync_time_ms_{0};   // Timestamp of last physical fdatasync()
};

} // namespace kv::persist
