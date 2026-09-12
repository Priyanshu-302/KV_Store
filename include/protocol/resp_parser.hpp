#pragma once

#include "common/types.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kv::proto {
  enum class ParseStatus : uint8_t {
    SUCCESS,
    INCOMPLETE,
    ERROR
  };

  class RespParser {
  public:
    RespParser();
    ~RespParser() = default;

    ParseStatus parse(const uint8_t* data, size_t len, size_t& bytes_consumed, Command& out_cmd);

    void reset() noexcept;
  private:
    enum class State : uint8_t {
      WAITING_TYPE,
      READING_ARRAY_LEN,
      READING_BULK_LEN,
      READING_BULK_DATA,
      READING_BULK_CRLF,
      READING_INLINE_CMD
    };

    State state_{State::WAITING_TYPE};

    int64_t expected_elements_{0}; 
    int64_t elements_parsed_{0}; 

    int64_t expected_bulk_len_{0}; 
    int64_t bulk_bytes_read_{0};

    std::string current_token_;

    Command pending_command_;
  };
  
}