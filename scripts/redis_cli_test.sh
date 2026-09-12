#!/usr/bin/env bash
# ==============================================================================
# End-to-End Integration Test Suite via Official redis-cli
# ==============================================================================

set -eo pipefail

HOST="${1:-127.0.0.1}"
PORT="${2:-6379}"

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

assert_eq() {
    local cmd="$1"
    local expected="$2"
    local actual
    actual=$(eval "$cmd")

    if [ "$actual" == "$expected" ]; then
        echo -e "[PASS] ${cmd} => ${GREEN}${actual}${NC}"
    else
        echo -e "[FAIL] ${cmd}"
        echo -e "       Expected: '${expected}'"
        echo -e "       Got:      '${actual}'"
        exit 1
    fi
}

echo "=================================================================="
echo "  Executing Integration Tests Against Key-Value Server ($HOST:$PORT)"
echo "=================================================================="

# Check if redis-cli is installed
if ! command -v redis-cli &> /dev/null; then
    echo -e "${RED}Error: redis-cli command not found on host system.${NC}"
    echo "Install via: sudo apt-get install redis-tools (Debian/Ubuntu) or brew install redis (macOS)"
    exit 1
fi

# Clean state before testing
redis-cli -h "$HOST" -p "$PORT" FLUSHDB > /dev/null

echo "--- 1. Connection & Handshake ---"
assert_eq "redis-cli -h $HOST -p $PORT PING" "PONG"
assert_eq "redis-cli -h $HOST -p $PORT PING 'Hello World'" "Hello World"
assert_eq "redis-cli -h $HOST -p $PORT ECHO 'Systems Engineering'" "Systems Engineering"

echo "--- 2. String Read & Write Operations ---"
assert_eq "redis-cli -h $HOST -p $PORT SET test_key 'Production Quality'" "OK"
assert_eq "redis-cli -h $HOST -p $PORT GET test_key" "Production Quality"
assert_eq "redis-cli -h $HOST -p $PORT GET missing_key" ""

echo "--- 3. Key Overwrite & Existence ---"
assert_eq "redis-cli -h $HOST -p $PORT SET test_key 'New Value'" "OK"
assert_eq "redis-cli -h $HOST -p $PORT GET test_key" "New Value"
assert_eq "redis-cli -h $HOST -p $PORT EXISTS test_key" "1"
assert_eq "redis-cli -h $HOST -p $PORT EXISTS missing_key" "0"

echo "--- 4. Deletions & Counters ---"
assert_eq "redis-cli -h $HOST -p $PORT SET key_a val_a" "OK"
assert_eq "redis-cli -h $HOST -p $PORT SET key_b val_b" "OK"
assert_eq "redis-cli -h $HOST -p $PORT DEL key_a key_b" "2"
assert_eq "redis-cli -h $HOST -p $PORT DEL key_a" "0"

echo "--- 5. TTL & Expiration Mechanics ---"
assert_eq "redis-cli -h $HOST -p $PORT SET session_key 'token123' EX 2" "OK"
assert_eq "redis-cli -h $HOST -p $PORT GET session_key" "token123"

# Sleep 3 seconds to trigger passive expiration
echo "Sleeping 3s to let TTL elapse..."
sleep 3
assert_eq "redis-cli -h $HOST -p $PORT GET session_key" ""
assert_eq "redis-cli -h $HOST -p $PORT TTL session_key" "-2"

echo "--- 6. Database Sizing & Clearing ---"
redis-cli -h "$HOST" -p "$PORT" SET k1 v1 > /dev/null
redis-cli -h "$HOST" -p "$PORT" SET k2 v2 > /dev/null
assert_eq "redis-cli -h $HOST -p $PORT DBSIZE" "2"
assert_eq "redis-cli -h $HOST -p $PORT FLUSHDB" "OK"
assert_eq "redis-cli -h $HOST -p $PORT DBSIZE" "0"

echo "=================================================================="
echo -e "${GREEN}All Official redis-cli Integration Tests Passed Successfully!${NC}"
echo "=================================================================="
