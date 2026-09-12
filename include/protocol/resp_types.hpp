#pragma once

#include <string>
#include <vector>
#include <variant>
#include <cstdint>
#include <string_view>

namespace kv::proto {
  enum class RespType : char {
    SIMPLE_STRING = '+',
    ERROR = '-',
    INTEGER = ':',
    BULK_STRING = '$',
    ARRAY = '*'
  };

  struct RespNil {
    constexpr bool operator==(const RespNil&) const noexcept = default;
  };

  struct RespObject;

  using RespValue = std::variant<std::string, int64_t, std::vector<RespObject>, RespNil>;

  struct RespObject {
    RespType type{RespType::SIMPLE_STRING};
    RespValue value;

    [[nodiscard]] bool is_nil() const noexcept {
        return std::holds_alternative<RespNil>(value);
    }

    [[nodiscard]] std::string_view as_string() const noexcept {
        if (std::holds_alternative<std::string>(value)) {
            return std::get<std::string>(value);
        }
        return {};
    }

    [[nodiscard]] int64_t as_integer() const noexcept {
        if (std::holds_alternative<int64_t>(value)) {
            return std::get<int64_t>(value);
        }
        return 0;
    }
  };
  
}