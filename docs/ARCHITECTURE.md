# Architecture — SocketAuction

## Goal

Build a real-time auction system in C using TCP sockets and POSIX threads, with multiple clients connected simultaneously.

## Overview

```text
client.c -- TCP --> server.c
                     |-- client thread 1
                     |-- client thread 2
                     |-- client thread 3
                     `-- shared auction state
```

## Client responsibilities

- connect to the server IP/port;
- identify the user with `LOGIN`;
- send protocol commands from the main thread;
- run a dedicated receiver thread;
- display normal responses and asynchronous events while the user can continue typing.

## Server responsibilities

- create and configure the TCP socket;
- accept connections;
- create one thread per client;
- validate commands;
- maintain shared auction state;
- protect shared state with a mutex;
- broadcast accepted bids;
- detect disconnects and release resources.

## Shared state

```text
AuctionState                 (implemented in M4)
- item
- current_bid
- highest_bidder
- dedicated mutex

ClientRegistry               (implemented in M3)
- active entries
- socket descriptor
- username
- dedicated mutex

BidHistory                   (implemented in M7)
- last 32 accepted bids
- username
- amount
- chronological order
```

## Concurrency rules

The user registry is shared by all server-side client threads, so register, unregister and user-list snapshots use a dedicated mutex.

The auction uses a separate mutex around the entire compare-and-update sequence. This prevents a race in which two client threads read the same old bid and then overwrite each other. M5 stress-tests this critical section with 20 bidders in the same time window for 5 consecutive rounds.

M6 adds asynchronous server-to-client traffic. A send mutex serializes writes so a direct response and a broadcast cannot interleave bytes on the same TCP stream. The client now has a dedicated receiver thread, allowing events to appear even when the user has not typed a new command.

M7 stores accepted bids inside the auction state while the auction mutex is held, so the visible highest bid and history are updated consistently. New clients also receive the current auction state immediately after login.

M8 hardens protocol framing and process lifecycle: oversized lines are completely drained before the next command is parsed, disconnected peers cannot terminate the server through `SIGPIPE`, and termination signals close the listening socket cleanly. The final `make test-all` suite runs concurrency, broadcast, history and robustness regressions.

## Initial technical decisions

- transport: TCP;
- application framing: one textual message per line;
- field separator: `|`;
- default port: 8080;
- maximum initial message size: 512 bytes;
- language: C11;
- target: Linux + GCC.

## Thread lifecycle in M2

For every successful `accept`, the server allocates storage for the client socket descriptor, starts a `pthread`, and immediately detaches it. The worker thread owns that descriptor, runs the existing client handler, closes the socket, and then exits. This lets the main thread return to `accept` immediately and serve other clients concurrently.

## Milestones

1. M1 — basic TCP connection and request/response. ✅
2. M2 — multiple clients using `pthread`. ✅
3. M3 — login and connected-user registry. ✅
4. M4 — bids and validation. ✅
5. M5 — simultaneous-bid concurrency stress tests. ✅
6. M6 — broadcast new bids. ✅
7. M7 — bid history and refinements. ✅
8. M8 — failure handling and final tests. ✅
