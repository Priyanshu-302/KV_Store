#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kv {
enum class StatusCode : uint8_t {
  OK = 0,
  ERR,
  ERR_SYNTAX,
  ERR_WRONG_TYPE,
  ERR_NOT_FOUND,
  ERR_OOM,
  ERR_IO,
  ERR_CLIENT_LIMIT
};

constexpr std::string_view status_code_to_string(StatusCode code) noexcept {
  switch (code) {
  case StatusCode::OK:
    return "OK";
  case StatusCode::ERR:
    return "ERR generic error";
  case StatusCode::ERR_SYNTAX:
    return "ERR syntax error";
  case StatusCode::ERR_WRONG_TYPE:
    return "WRONGTYPE Operation against a key holding the wrong kind of value";
  case StatusCode::ERR_NOT_FOUND:
    return "ERR no such key";
  case StatusCode::ERR_OOM:
    return "OOM command not allowed when used memory > 'maxmemory'";
  case StatusCode::ERR_IO:
    return "ERR disk or network I/O error";
  case StatusCode::ERR_CLIENT_LIMIT:
    return "ERR client output buffer limit exceeded";
  }
  return "ERR unknown error";
}

enum class FsyncPolicy: uint_t {
    ALWAYS,
    EVERYSEC,
    NO
};

inline FsyncPolicy fsync_policy_from_string(std::string_view str) {
    if (str == "always") return FsyncPolicy::ALWAYS;
    if (str == "no") return FsyncPolicy::NO;
    return FsyncPolicy::EVERYSEC; // Default
}

constexpr std::string_view fsync_policy_to_string(FsyncPolicy policy) {
    switch (policy) {
        case FsyncPolicy::ALWAYS:   return "always";
        case FsyncPolicy::EVERYSEC: return "everysec";
        case FsyncPolicy::NO:       return "no";
    }

    return "everysec";
}

// Redis Client Command e.g. SET, GET, EXPIRE, DEL
struct Command {
  std::string name;
  std::vector<std::string> args;
  
  void normalize_name() noexcept {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
      return static_cast<char>(std::toupper(c));
    });
  }
};

// Server Configuration
struct ServerConfig {
  std::string bind_address{"0.0.0.0"};
  uint16_t port{6379};
  int tcp_backlog{1024};
  bool tcp_nodelay{true};
  int client_timeout_sec{300};

  size_t max_memory_bytes{256 * 1024 * 1024};
  uint32_t max_clients{10000};

  size_t client_output_buffer_limit_hard{64 * 1024 * 1024}; 
  size_t client_output_buffer_limit_soft{16 * 1024 * 1024}; 
  int client_output_buffer_soft_seconds{10};

  bool aof_enabled{true};
  FsyncPolicy aof_fsync{FsyncPolicy::EVERYSEC};
  std::string aof_filename{"appendonly.aof"};

  std::string log_level{"info"};
};

// TTL expiration timestamp
inline int64_t current_time_ms() noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()
  ).count();
}

// loop execution times
inline int64_t monotonic_time_ms() noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()
  ).count();
}

} // namespace kv