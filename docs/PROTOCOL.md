# Protocol — SocketAuction

TCP is a byte stream, so the application must define message boundaries. SocketAuction uses one UTF-8/text command per line, terminated by `\n`. Fields are separated by `|`.

## Client -> server

```text
LOGIN|<name>
STATUS
BID|<amount>
USERS
HISTORY
PING
QUIT
```

Examples:

```text
LOGIN|Ana
BID|2500
```

## Server -> client

```text
OK|LOGIN|<name>
USERS|<count>|<user1>|<user2>|...
HISTORY|<count>|<user1>|<amount1>|<user2>|<amount2>|...
AUCTION|<item>|<highest_bid>|<highest_bidder>
BID_ACCEPTED|<amount>|<user>
BID_REJECTED|<reason>
EVENT|NEW_BID|<amount>|<user>
ERROR|<code>|<message>
PONG
BYE
```

## Login and user-registry rules

- `LOGIN` registers the connection in the server's shared user registry;
- usernames must be non-empty, at most 31 characters, and cannot contain `|`;
- two simultaneously connected clients cannot use the same username;
- a connection cannot change username after a successful login;
- `USERS` requires login and returns a snapshot of logged-in users;
- `HISTORY` requires login and returns accepted bids in chronological order;
- the server removes a logged-in user on `QUIT` or connection loss;
- the registry is protected with a dedicated mutex because multiple client threads access it concurrently.

## Auction rules

- a client must log in before bidding;
- `BID|<amount>` accepts positive integer values only;
- a bid must be strictly greater than the current bid;
- an accepted bid updates both the current value and highest bidder;
- `STATUS` returns the current shared auction state;
- the server is the only source of truth for auction state;
- clients only update their displayed state after a server response/event;
- malformed or unknown commands return an error instead of crashing the server;
- commands larger than the receive buffer are drained completely and return `ERROR|MESSAGE_TOO_LONG|Command exceeds maximum size`, preserving framing for the next command;
- after a bid is accepted, the bidder receives `BID_ACCEPTED|<amount>|<user>`;
- every other logged-in client receives `EVENT|NEW_BID|<amount>|<user>` asynchronously;
- clients do not need to send `STATUS` to learn about a new accepted bid;
- immediately after a successful login, the server sends the current `AUCTION|...` state automatically;
- the server keeps the 32 most recent accepted bids; rejected bids never enter history.
