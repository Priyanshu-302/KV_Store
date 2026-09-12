#include "protocol/resp_serializer.hpp"

namespace kv::proto {

std::string RespSerializer::serialize_simple_string(std::string_view str) {
    std::string out;
    out.reserve(str.size() + 3); // '+' + str + "\r\n"
    out.push_back('+');
    out.append(str);
    out.append("\r\n");
    return out;
}

std::string RespSerializer::serialize_error(std::string_view err_msg) {
    std::string out;
    out.reserve(err_msg.size() + 3); // '-' + err_msg + "\r\n"
    out.push_back('-');
    out.append(err_msg);
    out.append("\r\n");
    return out;
}

std::string RespSerializer::serialize_integer(int64_t val) {
    std::string out;
    out.push_back(':');
    out.append(std::to_string(val));
    out.append("\r\n");
    return out;
}

std::string RespSerializer::serialize_bulk_string(std::string_view str) {
    std::string len_str = std::to_string(str.size());
    std::string out;
    out.reserve(1 + len_str.size() + 2 + str.size() + 2); // '$' + len + "\r\n" + str + "\r\n"
    out.push_back('$');
    out.append(len_str);
    out.append("\r\n");
    out.append(str);
    out.append("\r\n");
    return out;
}

std::string RespSerializer::serialize_null() {
    return "$-1\r\n";
}

std::string RespSerializer::serialize_null_array() {
    return "*-1\r\n";
}

std::string RespSerializer::serialize_array(const std::vector<std::string>& elements) {
    std::string count_str = std::to_string(elements.size());
    std::string out;
    
    // Estimate initial capacity
    size_t estimated_size = 1 + count_str.size() + 2;
    for (const auto& elem : elements) {
        estimated_size += 1 + 8 + 2 + elem.size() + 2;
    }
    out.reserve(estimated_size);

    out.push_back('*');
    out.append(count_str);
    out.append("\r\n");

    for (const auto& elem : elements) {
        out.append(serialize_bulk_string(elem));
    }
    return out;
}

std::string RespSerializer::serialize_raw_array(const std::vector<std::string>& serialized_items) {
    std::string count_str = std::to_string(serialized_items.size());
    std::string out;
    out.push_back('*');
    out.append(count_str);
    out.append("\r\n");

    for (const auto& item : serialized_items) {
        out.append(item);
    }
    return out;
}

} // namespace kv::proto
