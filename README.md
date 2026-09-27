# SocketAuction

Real-time auction system built in C using TCP sockets, multithreading and client-server architecture.

## Objective

Build an online auction system for the Computer Networks course, focusing on TCP sockets, client-server communication, multiple simultaneous connections, threads, synchronization and connection-failure handling.

## Current progress

- [x] M1 — basic TCP connection and request/response.
- [x] M2 — multiple simultaneous clients with detached pthreads.
- [x] M3 — user login and synchronized connected-user registry.
- [x] M4 — bid validation and shared auction state.
- [x] M5 — simultaneous-bid concurrency stress tests.
- [x] M6 — real-time broadcast of accepted bids to connected clients.
- [x] M7 — bid history and automatic state sync for new clients.
- [x] M8 — failure handling, protocol hardening and final regression tests.

## Architecture

See `docs/ARCHITECTURE.md`.

## Protocol

See `docs/PROTOCOL.md`.

## Development workflow

See `docs/DEVELOPMENT.md` and `docs/TEST_PLAN.md`.

## Build

```bash
make
```

## Run

Server:

```bash
./server 8080
```

Client:

```bash
./client 127.0.0.1 8080
```

Current commands:

```text
LOGIN|Ana
BID|1500
USERS
HISTORY
STATUS
PING
QUIT
```

## Automated tests

M5 concurrency stress test:

```bash
make test-m5
```

It launches 20 bidders in the same time window and verifies the final maximum bid for 5 rounds.

M6 real-time broadcast test:

```bash
make test-m6
```

It keeps one client connected as an observer, lets another client place a bid, and verifies that the observer receives `EVENT|NEW_BID|...` without requesting `STATUS`.

M7 history/refinement test:

```bash
make test-m7
```

It verifies automatic auction-state delivery after login, chronological bid history, rejection of pre-login `HISTORY`, and exclusion of rejected bids from history.

M8 robustness test:

```bash
make test-m8
```

It verifies oversized-message handling, abrupt disconnect cleanup, malformed commands, continued server availability and graceful shutdown.

Run the complete regression suite with:

```bash
make test-all
```

## Failure handling

- oversized protocol lines are rejected and drained without desynchronizing the TCP stream;
- abrupt client disconnects remove the user from the registry;
- malformed commands return errors instead of terminating the server;
- `SIGPIPE` is ignored so a dead client cannot kill the server during a send/broadcast;
- `SIGINT` and `SIGTERM` trigger graceful listener shutdown.

## Environment

Fill before delivery:

- Linux distribution/version used in initial tests: Ubuntu 22.04.3 LTS (WSL2)
- GCC version used in initial tests: GCC 11.4.0
- Team members: __________
