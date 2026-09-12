#include "protocol/resp_parser.hpp"
#include <sstream>

namespace kv::proto {

RespParser::RespParser() {
    reset();
}

void RespParser::reset() noexcept {
    state_ = State::WAITING_TYPE;
    expected_elements_ = 0;
    elements_parsed_ = 0;
    expected_bulk_len_ = 0;
    bulk_bytes_read_ = 0;
    current_token_.clear();
    pending_command_.name.clear();
    pending_command_.args.clear();
}

ParseStatus RespParser::parse(const uint8_t* data, size_t len, size_t& bytes_consumed, Command& out_cmd) {
    size_t cursor = 0;
    bytes_consumed = 0;

    while (cursor < len) {
        switch (state_) {
            case State::WAITING_TYPE: {
                char prefix = static_cast<char>(data[cursor++]);

                if (prefix == '*') {
                    // Standard multi-bulk array command (*<count>\r\n)
                    expected_elements_ = 0;
                    elements_parsed_ = 0;
                    pending_command_.name.clear();
                    pending_command_.args.clear();
                    state_ = State::READING_ARRAY_LEN;
                } else if (prefix == '\r' || prefix == '\n') {
                    // Skip redundant standalone newlines
                    continue;
                } else {
                    // Fallback to inline command (Telnet compatibility: "PING\r\n")
                    current_token_.clear();
                    current_token_.push_back(prefix);
                    state_ = State::READING_INLINE_CMD;
                }
                break;
            }

            case State::READING_ARRAY_LEN: {
                // Read digits until CRLF
                while (cursor < len) {
                    char ch = static_cast<char>(data[cursor++]);
                    if (ch == '\r') {
                        // Expect trailing \n next
                        continue;
                    } else if (ch == '\n') {
                        if (expected_elements_ <= 0) {
                            // Empty array or null command
                            reset();
                            bytes_consumed = cursor;
                            return ParseStatus::SUCCESS;
                        }
                        state_ = State::READING_BULK_LEN;
                        break;
                    } else if (ch >= '0' && ch <= '9') {
                        expected_elements_ = (expected_elements_ * 10) + (ch - '0');
                    } else {
                        reset();
                        return ParseStatus::ERROR;
                    }
                }
                break;
            }

            case State::READING_BULK_LEN: {
                // Must see '$' prefix for each element in the multi-bulk array
                if (cursor < len && data[cursor] == '$') {
                    cursor++;
                }

                expected_bulk_len_ = 0;
                bool is_negative = false;

                while (cursor < len) {
                    char ch = static_cast<char>(data[cursor++]);
                    if (ch == '-') {
                        is_negative = true;
                    } else if (ch == '\r') {
                        continue;
                    } else if (ch == '\n') {
                        if (is_negative) {
                            // Null bulk string ($-1\r\n) treated as empty token
                            current_token_.clear();
                            elements_parsed_++;
                            if (pending_command_.name.empty()) {
                                pending_command_.name = current_token_;
                            } else {
                                pending_command_.args.push_back(current_token_);
                            }
                            if (elements_parsed_ == expected_elements_) {
                                pending_command_.normalize_name();
                                out_cmd = std::move(pending_command_);
                                reset();
                                bytes_consumed = cursor;
                                return ParseStatus::SUCCESS;
                            }
                        } else {
                            current_token_.clear();
                            current_token_.reserve(static_cast<size_t>(expected_bulk_len_));
                            bulk_bytes_read_ = 0;
                            state_ = State::READING_BULK_DATA;
                        }
                        break;
                    } else if (ch >= '0' && ch <= '9') {
                        expected_bulk_len_ = (expected_bulk_len_ * 10) + (ch - '0');
                    } else {
                        reset();
                        return ParseStatus::ERROR;
                    }
                }
                break;
            }

            case State::READING_BULK_DATA: {
                size_t remaining_in_token = static_cast<size_t>(expected_bulk_len_ - bulk_bytes_read_);
                size_t available_bytes = len - cursor;
                size_t bytes_to_copy = std::min(remaining_in_token, available_bytes);

                current_token_.append(reinterpret_cast<const char*>(data + cursor), bytes_to_copy);
                cursor += bytes_to_copy;
                bulk_bytes_read_ += static_cast<int64_t>(bytes_to_copy);

                if (bulk_bytes_read_ == expected_bulk_len_) {
                    state_ = State::READING_BULK_CRLF;
                }
                break;
            }

            case State::READING_BULK_CRLF: {
                // Must consume trailing \r\n after bulk payload
                while (cursor < len) {
                    char ch = static_cast<char>(data[cursor++]);
                    if (ch == '\r') {
                        continue;
                    } else if (ch == '\n') {
                        // Successfully completed an argument token
                        if (elements_parsed_ == 0) {
                            pending_command_.name = std::move(current_token_);
                        } else {
                            pending_command_.args.push_back(std::move(current_token_));
                        }
                        current_token_.clear();
                        elements_parsed_++;

                        if (elements_parsed_ == expected_elements_) {
                            // All array elements parsed!
                            pending_command_.normalize_name();
                            out_cmd = std::move(pending_command_);
                            reset();
                            bytes_consumed = cursor;
                            return ParseStatus::SUCCESS;
                        }

                        // Next token in array
                        state_ = State::READING_BULK_LEN;
                        break;
                    } else {
                        reset();
                        return ParseStatus::ERROR;
                    }
                }
                break;
            }

            case State::READING_INLINE_CMD: {
                // Telnet inline: read line until \n, split by space
                while (cursor < len) {
                    char ch = static_cast<char>(data[cursor++]);
                    if (ch == '\r') {
                        continue;
                    } else if (ch == '\n') {
                        // Tokenize line by whitespace
                        std::istringstream iss(current_token_);
                        std::string word;
                        if (iss >> word) {
                            pending_command_.name = word;
                            while (iss >> word) {
                                pending_command_.args.push_back(word);
                            }
                        }

                        if (!pending_command_.name.empty()) {
                            pending_command_.normalize_name();
                            out_cmd = std::move(pending_command_);
                            reset();
                            bytes_consumed = cursor;
                            return ParseStatus::SUCCESS;
                        }
                        reset();
                        break;
                    } else {
                        current_token_.push_back(ch);
                    }
                }
                break;
            }
        }
    }

    // Input buffer exhausted while mid-frame; caller must retain unparsed bytes
    bytes_consumed = cursor;
    return ParseStatus::INCOMPLETE;
}

} // namespace kv::proto
