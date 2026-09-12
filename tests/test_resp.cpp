#include <catch2/catch_test_macros.hpp>
#include "protocol/resp_parser.hpp"
#include "protocol/resp_serializer.hpp"
#include <string>
#include <vector>

TEST_CASE("RESP Serializer Output Compliance", "[resp][serializer]") {
    SECTION("Simple String serialization") {
        REQUIRE(kv::proto::RespSerializer::serialize_simple_string("OK") == "+OK\r\n");
        REQUIRE(kv::proto::RespSerializer::serialize_simple_string("PONG") == "+PONG\r\n");
    }

    SECTION("Error serialization") {
        REQUIRE(kv::proto::RespSerializer::serialize_error("ERR syntax error") == "-ERR syntax error\r\n");
    }

    SECTION("Integer serialization") {
        REQUIRE(kv::proto::RespSerializer::serialize_integer(100) == ":100\r\n");
        REQUIRE(kv::proto::RespSerializer::serialize_integer(0) == ":0\r\n");
        REQUIRE(kv::proto::RespSerializer::serialize_integer(-5) == ":-5\r\n");
    }

    SECTION("Bulk String serialization") {
        REQUIRE(kv::proto::RespSerializer::serialize_bulk_string("hello") == "$5\r\nhello\r\n");
        REQUIRE(kv::proto::RespSerializer::serialize_bulk_string("") == "$0\r\n\r\n");
        REQUIRE(kv::proto::RespSerializer::serialize_null() == "$-1\r\n");
    }

    SECTION("Array serialization") {
        std::vector<std::string> args = {"SET", "mykey", "myval"};
        std::string expected = "*3\r\n$3\r\nSET\r\n$5\r\nmykey\r\n$5\r\nmyval\r\n";
        REQUIRE(kv::proto::RespSerializer::serialize_array(args) == expected);
    }
}

TEST_CASE("RESP Parser Multi-Bulk Array & Inline Commands", "[resp][parser]") {
    kv::proto::RespParser parser;

    SECTION("Parses standard SET command") {
        std::string raw = "*3\r\n$3\r\nSET\r\n$4\r\nuser\r\n$4\r\njohn\r\n";
        size_t consumed = 0;
        kv::Command cmd;

        auto status = parser.parse(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), consumed, cmd);
        REQUIRE(status == kv::proto::ParseStatus::SUCCESS);
        REQUIRE(consumed == raw.size());
        REQUIRE(cmd.name == "SET");
        REQUIRE(cmd.args.size() == 2);
        REQUIRE(cmd.args[0] == "user");
        REQUIRE(cmd.args[1] == "john");
    }

    SECTION("Parses Telnet inline command") {
        std::string raw = "PING\r\n";
        size_t consumed = 0;
        kv::Command cmd;

        auto status = parser.parse(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), consumed, cmd);
        REQUIRE(status == kv::proto::ParseStatus::SUCCESS);
        REQUIRE(cmd.name == "PING");
        REQUIRE(cmd.args.empty());
    }
}

TEST_CASE("RESP Streaming FSM Byte-by-Byte TCP Fragmentation Fuzzing", "[resp][fsm]") {
    // Simulates worst-case network scenario: frame delivered 1 byte per TCP segment
    std::string wire_frame = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    kv::proto::RespParser parser;
    kv::Command final_cmd;
    bool completed = false;

    for (size_t i = 0; i < wire_frame.size(); ++i) {
        uint8_t byte = static_cast<uint8_t>(wire_frame[i]);
        size_t consumed = 0;
        kv::Command cmd;

        auto status = parser.parse(&byte, 1, consumed, cmd);

        if (i == wire_frame.size() - 1) {
            // Final byte should complete the command
            REQUIRE(status == kv::proto::ParseStatus::SUCCESS);
            final_cmd = std::move(cmd);
            completed = true;
        } else {
            // Intermediate bytes must maintain internal FSM state
            REQUIRE(status == kv::proto::ParseStatus::INCOMPLETE);
        }
    }

    REQUIRE(completed);
    REQUIRE(final_cmd.name == "SET");
    REQUIRE(final_cmd.args.size() == 2);
    REQUIRE(final_cmd.args[0] == "foo");
    REQUIRE(final_cmd.args[1] == "bar");
}

TEST_CASE("RESP Multi-Command Pipeline Processing", "[resp][pipeline]") {
    // Two back-to-back concatenated commands in a single TCP receive buffer
    std::string stream = "*1\r\n$4\r\nPING\r\n*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n";
    kv::proto::RespParser parser;

    size_t offset = 0;
    std::vector<kv::Command> parsed_commands;

    while (offset < stream.size()) {
        size_t consumed = 0;
        kv::Command cmd;
        auto status = parser.parse(
            reinterpret_cast<const uint8_t*>(stream.data() + offset),
            stream.size() - offset,
            consumed,
            cmd
        );

        if (status == kv::proto::ParseStatus::SUCCESS) {
            parsed_commands.push_back(std::move(cmd));
            offset += consumed;
        } else {
            break;
        }
    }

    REQUIRE(parsed_commands.size() == 2);
    REQUIRE(parsed_commands[0].name == "PING");
    REQUIRE(parsed_commands[1].name == "GET");
    REQUIRE(parsed_commands[1].args[0] == "foo");
}
