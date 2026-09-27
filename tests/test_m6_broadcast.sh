#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-18120}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

SERVER_PID=""

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

make >/dev/null

server_log="/tmp/socketauction_m6_server.log"
observer_log="/tmp/socketauction_m6_observer.log"
bidder_log="/tmp/socketauction_m6_bidder.log"

rm -f "$server_log" "$observer_log" "$bidder_log"

stdbuf -oL -eL ./server "$PORT" >"$server_log" 2>&1 &
SERVER_PID=$!
sleep 0.30

{
    {
        printf 'LOGIN|Joao\n'
        sleep 1.50
        printf 'QUIT\n'
    } | ./client 127.0.0.1 "$PORT" >"$observer_log" 2>&1
} &
OBSERVER_PID=$!

sleep 0.35

printf 'LOGIN|Ana\nBID|2500\nQUIT\n' |
    ./client 127.0.0.1 "$PORT" >"$bidder_log" 2>&1

wait "$OBSERVER_PID"

if ! grep -Fq "BID_ACCEPTED|2500|Ana" "$bidder_log"; then
    echo "[FAIL] Bidder did not receive BID_ACCEPTED."
    cat "$bidder_log"
    exit 1
fi

if ! grep -Fq "EVENT|NEW_BID|2500|Ana" "$observer_log"; then
    echo "[FAIL] Observer did not receive the real-time bid event."
    echo "--- Observer ---"
    cat "$observer_log"
    echo "--- Server ---"
    cat "$server_log"
    exit 1
fi

echo "[PASS] Bidder received BID_ACCEPTED|2500|Ana"
echo "[PASS] Observer received EVENT|NEW_BID|2500|Ana"
echo "[PASS] M6 real-time broadcast test completed."
