#include <catch2/catch_test_macros.hpp>
#include "persistence/aof.hpp"
#include "storage/engine.hpp"
#include "protocol/resp_serializer.hpp"

#include <cstdio>
#include <thread>
#include <chrono>

TEST_CASE("Append-Only File (AOF) Crash Recovery & Replay", "[aof]") {
    const std::string test_aof_file = "test_appendonly.aof";
    // Ensure clean state before testing
    std::remove(test_aof_file.c_str());

    // Phase 1: Write operations and persist to AOF
    {
        kv::storage::Engine engine(1024 * 1024);
        kv::persist::AofLogger aof(test_aof_file, kv::FsyncPolicy::ALWAYS);
        aof.open();

        // Write SET keys
        engine.set("user:1", "alice");
        aof.append(kv::proto::RespSerializer::serialize_array({"SET", "user:1", "alice"}));

        engine.set("user:2", "bob");
        aof.append(kv::proto::RespSerializer::serialize_array({"SET", "user:2", "bob"}));

        // Write DEL key
        engine.del("user:1");
        aof.append(kv::proto::RespSerializer::serialize_array({"DEL", "user:1"}));

        // Close simulated server (flushes and closes descriptor)
        aof.close();
    }

    // Phase 2: Start brand-new empty engine and replay the log
    {
        kv::storage::Engine recovered_engine(1024 * 1024);
        REQUIRE(recovered_engine.key_count() == 0);

        kv::persist::AofLogger replayer(test_aof_file, kv::FsyncPolicy::ALWAYS);
        REQUIRE(replayer.replay(recovered_engine) == true);

        // Verify state is accurately reconstructed
        REQUIRE(recovered_engine.key_count() == 1);
        REQUIRE_FALSE(recovered_engine.exists("user:1")); // Was deleted
        REQUIRE(recovered_engine.exists("user:2"));       // Still active
        REQUIRE(recovered_engine.get("user:2").value() == "bob");
    }

    // Phase 3: Cleanup disk artifact
    std::remove(test_aof_file.c_str());
}

TEST_CASE("AOF Absolute TTL Timestamp Replay (PEXPIREAT)", "[aof][ttl]") {
    const std::string test_ttl_aof = "test_ttl.aof";
    std::remove(test_ttl_aof.c_str());

    {
        kv::persist::AofLogger aof(test_ttl_aof, kv::FsyncPolicy::ALWAYS);
        aof.open();

        // Write a key that expires 5 seconds in the future
        int64_t future_expire_at = kv::current_time_ms() + 5000;
        std::vector<std::string> args = {
            "SET", "session_token", "xyz123", "PEXPIREAT", std::to_string(future_expire_at)
        };
        aof.append(kv::proto::RespSerializer::serialize_array(args));
        aof.close();
    }

    {
        kv::storage::Engine engine(1024 * 1024);
        kv::persist::AofLogger replayer(test_ttl_aof, kv::FsyncPolicy::ALWAYS);
        REQUIRE(replayer.replay(engine) == true);

        // Key should be alive with positive remaining TTL
        REQUIRE(engine.exists("session_token"));
        REQUIRE(engine.ttl("session_token") > 0);
    }

    std::remove(test_ttl_aof.c_str());
}
