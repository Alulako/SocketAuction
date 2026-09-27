# Test plan — SocketAuction

## M1

- [x] Server starts on the selected port.
- [x] Client connects locally.
- [x] PING receives PONG.
- [x] STATUS returns the initial auction state.
- [x] LOGIN returns a confirmation.
- [x] QUIT closes the session.
- [x] Unknown commands return an error.
- [x] Client handles an unavailable server.

## M2 — multiple clients

- [x] 2 clients can stay connected at once.
- [x] 5 clients can stay connected at once.

## M3 — login and connected-user registry

- [x] Successful login registers the username.
- [x] `USERS` returns all currently logged-in users.
- [x] Duplicate usernames are rejected while the first user is connected.
- [x] A connection cannot log in twice.
- [x] `USERS` is rejected before login.
- [x] Username is released after `QUIT`.
- [x] Username is released when the client closes the connection without `QUIT`.

## M4 — bids and validation

- [x] Bidding before login is rejected.
- [x] Non-numeric bid is rejected.
- [x] Bid equal to the current bid is rejected.
- [x] Bid below the current bid is rejected.
- [x] Valid bid above the current value is accepted.
- [x] Accepted bid updates `STATUS`.
- [x] Auction state is shared across different clients.
- [x] Highest bidder changes when another user places a higher bid.

## M5 — concurrent bids

- [x] Auction compare-and-update is protected by a dedicated mutex.
- [x] 20 clients can submit bids in the same time window.
- [x] The final bid is the global maximum after concurrent updates.
- [x] The final highest bidder matches the owner of the maximum bid.
- [x] Stress test passes for 5 consecutive rounds.

Run with:

```bash
make test-m5
```

## M6 — real-time broadcast

- [x] Bidder receives `BID_ACCEPTED`.
- [x] Another logged-in client receives `EVENT|NEW_BID` without issuing a command.
- [x] Client receiver thread can receive asynchronous server messages while the main thread handles user input.
- [x] Concurrent server writes are serialized.
- [x] Server ignores `SIGPIPE`, so a disconnected client does not terminate the process during broadcast.
- [x] M5 concurrency test still passes after broadcast was introduced.

Run with:

```bash
make test-m6
```

## M7 — history and state synchronization

- [x] `HISTORY` requires login.
- [x] New clients receive the current auction state automatically after login.
- [x] Accepted bids are stored in chronological order.
- [x] Rejected bids are not stored in history.
- [x] History is shared across different clients.
- [x] M5 and M6 regression tests still pass after M7 changes.

Run with:

```bash
make test-m7
```

## M8 — robustness and final regression

- [x] Oversized protocol line returns `ERROR|MESSAGE_TOO_LONG`.
- [x] Server drains an oversized line and correctly parses the next command.
- [x] Abrupt disconnect unregisters the user.
- [x] Malformed bids and unknown commands do not crash the server.
- [x] Server accepts fresh clients after failure scenarios.
- [x] `SIGTERM` performs graceful listener shutdown.
- [x] Full M5-M8 regression suite passes.

Run M8 only:

```bash
make test-m8
```

Run all automated tests:

```bash
make test-all
```

## Build validation

```bash
make clean
make
```

Compiler flags:

```text
-std=c11 -Wall -Wextra -Wpedantic
```
