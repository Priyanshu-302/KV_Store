#include <catch2/catch_test_macros.hpp>
#include "storage/engine.hpp"
#include <thread>
#include <chrono>

TEST_CASE("Storage Engine CRUD Operations", "[engine]") {
    kv::storage::Engine engine(1024 * 1024); // 1MB capacity

    SECTION("Basic SET and GET") {
        REQUIRE(engine.set("foo", "bar") == kv::StatusCode::OK);
        auto val = engine.get("foo");
        REQUIRE(val.has_value());
        REQUIRE(val.value() == "bar");
    }

    SECTION("GET non-existent key returns nullopt") {
        REQUIRE_FALSE(engine.get("non_existent").has_value());
    }

    SECTION("Key Overwrite Path") {
        REQUIRE(engine.set("key1", "val1") == kv::StatusCode::OK);
        REQUIRE(engine.set("key1", "val2") == kv::StatusCode::OK);
        auto val = engine.get("key1");
        REQUIRE(val.has_value());
        REQUIRE(val.value() == "val2");
    }

    SECTION("DEL removes existing key") {
        REQUIRE(engine.set("key_to_del", "val") == kv::StatusCode::OK);
        REQUIRE(engine.exists("key_to_del"));
        REQUIRE(engine.del("key_to_del") == true);
        REQUIRE_FALSE(engine.exists("key_to_del"));
        REQUIRE(engine.del("key_to_del") == false); // Second delete returns false
    }

    SECTION("FLUSHDB clears all keys") {
        engine.set("k1", "v1");
        engine.set("k2", "v2");
        REQUIRE(engine.key_count() == 2);
        engine.flushdb();
        REQUIRE(engine.key_count() == 0);
        REQUIRE_FALSE(engine.get("k1").has_value());
    }
}

TEST_CASE("Storage Engine $O(1)$ LRU Eviction Under Memory Pressure", "[engine][lru]") {
    // Set a tight memory budget (approx 3 entries before eviction triggers)
    const size_t tight_budget = 400; 
    kv::storage::Engine engine(tight_budget);

    engine.set("k1", "val1");
    engine.set("k2", "val2");
    engine.set("k3", "val3");

    SECTION("Oldest key (k1) is evicted first when memory budget is exceeded") {
        // Adding k4 should force eviction of k1 (tail of LRU)
        engine.set("k4", "val4");

        REQUIRE_FALSE(engine.exists("k1")); // k1 evicted
        REQUIRE(engine.exists("k2"));
        REQUIRE(engine.exists("k3"));
        REQUIRE(engine.exists("k4"));
    }

    SECTION("Accessing an older key promotes it to head, saving it from eviction") {
        // Read k1 -> promotes k1 to head (MRU), making k2 the oldest (LRU tail)
        REQUIRE(engine.get("k1").has_value());

        // Now insert k4 -> k2 should be evicted instead of k1!
        engine.set("k4", "val4");

        REQUIRE(engine.exists("k1"));       // Saved from eviction!
        REQUIRE_FALSE(engine.exists("k2")); // Evicted
        REQUIRE(engine.exists("k3"));
        REQUIRE(engine.exists("k4"));
    }
}

TEST_CASE("Dual-Phase TTL Expiration", "[engine][ttl]") {
    kv::storage::Engine engine(1024 * 1024);

    SECTION("Passive Expiration: Expired keys disappear immediately on GET") {
        // Set key with 20 millisecond TTL
        engine.set("temp_key", "temp_val", 20);
        REQUIRE(engine.exists("temp_key"));

        // Wait for TTL to elapse
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        // Lazy check on access should return nullopt and delete node
        REQUIRE_FALSE(engine.get("temp_key").has_value());
        REQUIRE_FALSE(engine.exists("temp_key"));
        REQUIRE(engine.ttl("temp_key") == -2); // -2 = not found/expired
    }

    SECTION("Active Expiration: Periodic sample deletes expired keys without client access") {
        // Insert 10 keys with 15ms TTL
        for (int i = 0; i < 10; ++i) {
            engine.set("auto_exp_" + std::to_string(i), "val", 15);
        }
        REQUIRE(engine.key_count() == 10);

        std::this_thread::sleep_for(std::chrono::milliseconds(25));

        // Run active expiration routine
        size_t purged = engine.active_expire_cycle(20, 25);
        REQUIRE(purged == 10);
        REQUIRE(engine.key_count() == 0);
    }
}
