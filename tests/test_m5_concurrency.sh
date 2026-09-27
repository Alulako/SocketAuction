#!/usr/bin/env bash
set -euo pipefail

PORT="${PORT:-18105}"
CLIENTS="${CLIENTS:-20}"
ROUNDS="${ROUNDS:-5}"
BASE_BID=1000

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

if (( CLIENTS < 2 || CLIENTS > 30 )); then
    echo "CLIENTS must be between 2 and 30."
    exit 1
fi

make >/dev/null

for round in $(seq 1 "$ROUNDS"); do
    server_log="/tmp/socketauction_m5_server_${round}.log"
    rm -f "$server_log"

    stdbuf -oL -eL ./server "$PORT" >"$server_log" 2>&1 &
    SERVER_PID=$!
    sleep 0.25

    expected_bid=$((BASE_BID + CLIENTS * 100))
    expected_user="User${CLIENTS}"

    client_pids=()

    for i in $(seq 1 "$CLIENTS"); do
        bid=$((BASE_BID + i * 100))
        client_log="/tmp/socketauction_m5_client_${round}_${i}.log"

        {
            {
                printf 'LOGIN|User%s\n' "$i"
                sleep 0.20
                printf 'BID|%s\nQUIT\n' "$bid"
            } | ./client 127.0.0.1 "$PORT" >"$client_log" 2>&1
        } &
        client_pids+=("$!")
    done

    for pid in "${client_pids[@]}"; do
        wait "$pid"
    done

    status_log="/tmp/socketauction_m5_status_${round}.log"
    printf 'LOGIN|Verifier%s\nSTATUS\nQUIT\n' "$round" |
        ./client 127.0.0.1 "$PORT" >"$status_log" 2>&1

    expected_status="AUCTION|Notebook|${expected_bid}|${expected_user}"

    if ! grep -Fq "$expected_status" "$status_log"; then
        echo "[FAIL] Round $round"
        echo "Expected: $expected_status"
        echo "Actual verifier output:"
        cat "$status_log"
        echo "Server log:"
        cat "$server_log"
        exit 1
    fi

    echo "[PASS] Round $round -> $expected_status"

    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
    PORT=$((PORT + 1))
done

echo "[PASS] M5 concurrency stress test completed: $ROUNDS rounds, $CLIENTS simultaneous bidders per round."
