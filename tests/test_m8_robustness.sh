#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-18140}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

SERVER_PID=""

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

make >/dev/null

server_log="/tmp/socketauction_m8_server.log"
raw_log="/tmp/socketauction_m8_raw.log"
reconnect_log="/tmp/socketauction_m8_reconnect.log"
malformed_log="/tmp/socketauction_m8_malformed.log"
alive_log="/tmp/socketauction_m8_alive.log"

rm -f "$server_log" "$raw_log" "$reconnect_log" "$malformed_log" "$alive_log"

stdbuf -oL -eL ./server "$PORT" >"$server_log" 2>&1 &
SERVER_PID=$!
sleep 0.30

# 1) Oversized raw protocol line must be rejected and fully drained.
exec 3<>"/dev/tcp/127.0.0.1/$PORT"
head -c 3000 /dev/zero | tr '\0' X >&3
printf '\nPING\n' >&3
IFS= read -r raw_first <&3
IFS= read -r raw_second <&3
printf '%s\n%s\n' "$raw_first" "$raw_second" >"$raw_log"
exec 3>&-
exec 3<&-

if [[ "$raw_first" != "ERROR|MESSAGE_TOO_LONG|Command exceeds maximum size" ]]; then
    echo "[FAIL] Oversized command was not rejected correctly."
    cat "$raw_log"
    exit 1
fi

if [[ "$raw_second" != "PONG" ]]; then
    echo "[FAIL] Server did not recover framing after oversized command."
    cat "$raw_log"
    exit 1
fi

# 2) Abrupt disconnect must unregister the username.
exec 4<>"/dev/tcp/127.0.0.1/$PORT"
printf 'LOGIN|CrashUser\n' >&4
IFS= read -r crash_login <&4
IFS= read -r crash_status <&4
exec 4>&-
exec 4<&-
sleep 0.20

printf 'LOGIN|CrashUser\nUSERS\nQUIT\n' |
    ./client 127.0.0.1 "$PORT" >"$reconnect_log" 2>&1

if ! grep -Fq "OK|LOGIN|CrashUser" "$reconnect_log"; then
    echo "[FAIL] Username was not released after abrupt disconnect."
    cat "$reconnect_log"
    exit 1
fi

# 3) Malformed input must not crash the server.
printf 'LOGIN|Guard\nBID|abc\nBID|-10\nWAT\nPING\nQUIT\n' |
    ./client 127.0.0.1 "$PORT" >"$malformed_log" 2>&1

grep -Fq "BID_REJECTED|Invalid bid amount" "$malformed_log"
grep -Fq "ERROR|UNKNOWN_COMMAND|Unknown command" "$malformed_log"
grep -Fq "PONG" "$malformed_log"

# 4) Server must still accept a fresh client after all failure cases.
printf 'LOGIN|Alive\nSTATUS\nPING\nQUIT\n' |
    ./client 127.0.0.1 "$PORT" >"$alive_log" 2>&1

grep -Fq "OK|LOGIN|Alive" "$alive_log"
grep -Fq "AUCTION|Notebook|" "$alive_log"
grep -Fq "PONG" "$alive_log"

# 5) Graceful SIGTERM shutdown should finish cleanly.
kill -TERM "$SERVER_PID"
wait "$SERVER_PID"
SERVER_PID=""

if ! grep -Fq "[SERVER] Shutdown complete." "$server_log"; then
    echo "[FAIL] Server did not report graceful shutdown."
    cat "$server_log"
    exit 1
fi

echo "[PASS] Oversized protocol messages are rejected and drained."
echo "[PASS] Abrupt disconnect releases the username."
echo "[PASS] Malformed commands do not crash the server."
echo "[PASS] Server remains usable after failure scenarios."
echo "[PASS] SIGTERM performs a graceful server shutdown."
echo "[PASS] M8 robustness test completed."
