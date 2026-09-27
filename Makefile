CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2
THREAD_FLAGS := -pthread

.PHONY: all clean run-server run-client test-m5 test-m6 test-m7 test-m8 test-all

all: server client

server: server.c
	$(CC) $(CFLAGS) $< -o $@ $(THREAD_FLAGS)

client: client.c
	$(CC) $(CFLAGS) $< -o $@ $(THREAD_FLAGS)

run-server: server
	./server 8080

run-client: client
	./client 127.0.0.1 8080

test-m5: all
	bash tests/test_m5_concurrency.sh

test-m6: all
	bash tests/test_m6_broadcast.sh

test-m7: all
	bash tests/test_m7_history.sh

test-m8: all
	bash tests/test_m8_robustness.sh

test-all: all
	bash tests/test_m5_concurrency.sh
	bash tests/test_m6_broadcast.sh
	bash tests/test_m7_history.sh
	bash tests/test_m8_robustness.sh

clean:
	rm -f server client *.o
