#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace kv::proto {
  class RespSerializer {
  public:
    [[nodiscard]] static std::string serialize_simple_string(std::string_view str);

    [[nodiscard]] static std::string serialize_error(std::string_view err_msg);

    [[nodiscard]] static std::string serialize_bulk_string(std::string_view str);

    [[nodiscard]] static std::string serialize_null();

    [[nodiscard]] static std::string serialize_integer(int64_t val);

    [[nodiscard]] static std::string serialize_null_array();

    [[nodiscard]] static std::string serialize_array(const std::vector<std::string>& elements);

    [[nodiscard]] static std::string serialize_raw_array(const std::vector<std::string>& serialized_items);
  };
  
}