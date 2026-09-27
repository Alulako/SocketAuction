#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-18130}"
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

server_log="/tmp/socketauction_m7_server.log"
prelogin_log="/tmp/socketauction_m7_prelogin.log"
ana_log="/tmp/socketauction_m7_ana.log"
joao_log="/tmp/socketauction_m7_joao.log"
bia_log="/tmp/socketauction_m7_bia.log"

rm -f "$server_log" "$prelogin_log" "$ana_log" "$joao_log" "$bia_log"

stdbuf -oL -eL ./server "$PORT" >"$server_log" 2>&1 &
SERVER_PID=$!
sleep 0.30

printf 'HISTORY\nQUIT\n' |
    ./client 127.0.0.1 "$PORT" >"$prelogin_log" 2>&1

printf 'LOGIN|Ana\nBID|1500\nQUIT\n' |
    ./client 127.0.0.1 "$PORT" >"$ana_log" 2>&1

printf 'LOGIN|Joao\nBID|1400\nBID|1800\nQUIT\n' |
    ./client 127.0.0.1 "$PORT" >"$joao_log" 2>&1

printf 'LOGIN|Bia\nHISTORY\nQUIT\n' |
    ./client 127.0.0.1 "$PORT" >"$bia_log" 2>&1

if ! grep -Fq "ERROR|NOT_LOGGED_IN|Login required" "$prelogin_log"; then
    echo "[FAIL] HISTORY was not rejected before login."
    cat "$prelogin_log"
    exit 1
fi

if ! grep -Fq "AUCTION|Notebook|1000|NONE" "$ana_log"; then
    echo "[FAIL] Ana did not receive the initial auction state automatically."
    cat "$ana_log"
    exit 1
fi

if ! grep -Fq "BID_REJECTED|Bid must be greater than 1500" "$joao_log"; then
    echo "[FAIL] Rejected bid behavior changed unexpectedly."
    cat "$joao_log"
    exit 1
fi

if ! grep -Fq "AUCTION|Notebook|1800|Joao" "$bia_log"; then
    echo "[FAIL] New client did not receive the current auction state automatically."
    cat "$bia_log"
    exit 1
fi

if ! grep -Fq "HISTORY|2|Ana|1500|Joao|1800" "$bia_log"; then
    echo "[FAIL] Bid history is incorrect or contains rejected bids."
    echo "--- Bia ---"
    cat "$bia_log"
    echo "--- Server ---"
    cat "$server_log"
    exit 1
fi

echo "[PASS] HISTORY requires login."
echo "[PASS] New clients receive the current auction state automatically."
echo "[PASS] HISTORY contains accepted bids in chronological order."
echo "[PASS] Rejected bids are not stored in history."
echo "[PASS] M7 history/refinement test completed."
