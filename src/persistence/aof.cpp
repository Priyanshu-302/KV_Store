#include "persistence/aof.hpp"
#include "protocol/resp_parser.hpp"
#include "common/logger.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace kv::persist {

AofLogger::AofLogger(std::string filename, FsyncPolicy policy)
    : filename_(std::move(filename)),
      policy_(policy),
      last_sync_time_ms_(current_time_ms()) {}

AofLogger::~AofLogger() {
    close();
}

void AofLogger::open() {
    if (fd_ >= 0) return;

    // Open for append-only writing; create with 0644 permissions if not present
    fd_ = ::open(filename_.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd_ < 0) {
        KV_LOG_ERROR("Failed to open AOF file '{}': {}", filename_, std::strerror(errno));
        throw std::runtime_error("Could not open AOF file");
    }

    KV_LOG_INFO("AOF persistence active on '{}' (fsync: {})", filename_, fsync_policy_to_string(policy_));
}

void AofLogger::close() noexcept {
    if (fd_ >= 0) {
        flush_buffer();
        if (policy_ != FsyncPolicy::NO) {
            sync_to_disk();
        }
        ::close(fd_);
        fd_ = -1;
    }
}

void AofLogger::append(std::string_view serialized_resp_cmd) {
    if (fd_ < 0 || serialized_resp_cmd.empty()) return;

    buffer_.append(serialized_resp_cmd);

    // If policy is ALWAYS, write and sync immediately on every command
    if (policy_ == FsyncPolicy::ALWAYS) {
        flush_buffer();
        sync_to_disk();
    }
}

void AofLogger::flush_buffer() {
    if (fd_ < 0 || buffer_.empty()) return;

    size_t total_written = 0;
    while (total_written < buffer_.size()) {
        const char* src = buffer_.data() + total_written;
        size_t remaining = buffer_.size() - total_written;

        ssize_t bytes_written = ::write(fd_, src, remaining);

        if (bytes_written > 0) {
            total_written += static_cast<size_t>(bytes_written);
        } else if (bytes_written < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal, retry write
            }
            KV_LOG_ERROR("POSIX write() failed to AOF: {}", std::strerror(errno));
            break;
        }
    }

    buffer_.clear();
}

void AofLogger::sync_to_disk() {
    if (fd_ < 0) return;

    // fdatasync flushes data pages without necessarily flushing file mtime metadata
    if (::fdatasync(fd_) < 0) {
        KV_LOG_ERROR("fdatasync() failed on AOF fd {}: {}", fd_, std::strerror(errno));
    } else {
        last_sync_time_ms_ = current_time_ms();
    }
}

void AofLogger::check_cron_sync() {
    // Under everysec policy, flush writes every cycle and sync every 1000ms
    flush_buffer();

    if (policy_ == FsyncPolicy::EVERYSEC) {
        int64_t now = current_time_ms();
        if ((now - last_sync_time_ms_) >= 1000) {
            sync_to_disk();
        }
    }
}

bool AofLogger::replay(storage::Engine& engine) {
    // Check if AOF file exists
    struct stat st{};
    if (::stat(filename_.c_str(), &st) != 0) {
        KV_LOG_INFO("No existing AOF file found ('{}'). Starting with empty database.", filename_);
        return true;
    }

    int read_fd = ::open(filename_.c_str(), O_RDONLY | O_CLOEXEC);
    if (read_fd < 0) {
        KV_LOG_ERROR("Failed to open AOF file for recovery replay: {}", std::strerror(errno));
        return false;
    }

    KV_LOG_INFO("Replaying AOF persistence log '{}' ({} bytes)...", filename_, st.st_size);

    proto::RespParser parser;
    std::vector<uint8_t> read_buf(64 * 1024); // 64KB read chunk buffer
    size_t unparsed_offset = 0;
    size_t valid_commands_replayed = 0;

    while (true) {
        ssize_t bytes_read = ::read(read_fd, read_buf.data() + unparsed_offset, read_buf.size() - unparsed_offset);

        if (bytes_read > 0) {
            size_t total_available = unparsed_offset + static_cast<size_t>(bytes_read);
            size_t cursor = 0;

            while (cursor < total_available) {
                size_t consumed = 0;
                Command cmd;
                proto::ParseStatus status = parser.parse(
                    read_buf.data() + cursor,
                    total_available - cursor,
                    consumed,
                    cmd
                );

                if (status == proto::ParseStatus::SUCCESS) {
                    cursor += consumed;

                    // Execute replayed command directly against the storage engine
                    if (cmd.name == "SET" && cmd.args.size() >= 2) {
                        int64_t ttl_ms = -1;

                        // Check for canonical PEXPIREAT <epoch_ms> parameter
                        if (cmd.args.size() >= 4 && cmd.args[2] == "PEXPIREAT") {
                            try {
                                int64_t expire_at = std::stoll(cmd.args[3]);
                                int64_t now = current_time_ms();
                                if (expire_at > now) {
                                    ttl_ms = expire_at - now;
                                } else {
                                    // Already expired prior to crash -> skip restoring
                                    continue;
                                }
                            } catch (...) {
                                ttl_ms = -1;
                            }
                        }
                        engine.set(cmd.args[0], cmd.args[1], ttl_ms);
                        valid_commands_replayed++;
                    } else if (cmd.name == "DEL" && !cmd.args.empty()) {
                        for (const auto& key : cmd.args) {
                            engine.del(key);
                        }
                        valid_commands_replayed++;
                    } else if (cmd.name == "FLUSHDB") {
                        engine.flushdb();
                        valid_commands_replayed++;
                    }
                } else if (status == proto::ParseStatus::INCOMPLETE) {
                    // Retain unparsed chunk at the front of the buffer
                    size_t remaining = total_available - cursor;
                    std::memmove(read_buf.data(), read_buf.data() + cursor, remaining);
                    unparsed_offset = remaining;
                    break;
                } else {
                    KV_LOG_ERROR("Malformed RESP command encountered during AOF replay. Stopping.");
                    ::close(read_fd);
                    return false;
                }
            }

            if (cursor == total_available) {
                unparsed_offset = 0;
            }
        } else if (bytes_read == 0) {
            // EOF reached
            break;
        } else {
            if (errno == EINTR) continue;
            KV_LOG_ERROR("AOF read error during replay: {}", std::strerror(errno));
            ::close(read_fd);
            return false;
        }
    }

    ::close(read_fd);
    KV_LOG_INFO("AOF replay complete! Recovered {} commands ({} keys in memory).",
                valid_commands_replayed, engine.key_count());
    return true;
}

} // namespace kv::persist
