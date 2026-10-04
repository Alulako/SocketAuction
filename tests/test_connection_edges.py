#!/usr/bin/env python3
"""Testes de enquadramento TCP, encerramento e ordem dos eventos."""
import signal
import socket
import subprocess
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read_line(connection):
    data = bytearray()
    while True:
        part = connection.recv(1)
        if not part:
            return None if not data else data.decode("ascii")
        if part == b"\n":
            return data.decode("ascii")
        data.extend(part)


def choose_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class SocketAuctionEdges(unittest.TestCase):
    def setUp(self):
        self.port = choose_port()
        self.server = subprocess.Popen(
            ["./server", str(self.port)], cwd=ROOT,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        for _ in range(40):
            try:
                connection = self.connect()
                connection.close()
                break
            except OSError:
                if self.server.poll() is not None:
                    self.fail("Servidor terminou antes de aceitar conexões")
                time.sleep(0.05)
        else:
            self.fail("Servidor não começou a escutar")

    def tearDown(self):
        if self.server.poll() is None:
            self.server.send_signal(signal.SIGTERM)
            try:
                self.server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.server.kill()
                self.server.wait()
                self.fail("Servidor não encerrou em cinco segundos")

    def connect(self):
        connection = socket.create_connection(("127.0.0.1", self.port), timeout=3)
        connection.settimeout(3)
        return connection

    def login(self, connection, name):
        connection.sendall(f"LOGIN|{name}\n".encode("ascii"))
        self.assertEqual(read_line(connection), f"OK|LOGIN|{name}")
        self.assertTrue(read_line(connection).startswith("AUCTION|"))

    def test_partial_frame_is_not_executed(self):
        with self.connect() as partial:
            partial.sendall(b"PING")
            partial.shutdown(socket.SHUT_WR)
            self.assertEqual(partial.recv(4096), b"")

        with self.connect() as bidder:
            self.login(bidder, "Incomplete")
            bidder.sendall(b"BID|9000")
            bidder.shutdown(socket.SHUT_WR)
            self.assertEqual(bidder.recv(4096), b"")

        with self.connect() as verifier:
            verifier.sendall(b"\nSTATUS\n")
            self.assertEqual(read_line(verifier),
                             "ERROR|EMPTY_COMMAND|Empty command")
            self.assertEqual(read_line(verifier), "AUCTION|Notebook|1000|NONE")

    def test_client_exits_when_server_shuts_down(self):
        client = subprocess.Popen(
            ["./client", "127.0.0.1", str(self.port)], cwd=ROOT,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True
        )
        try:
            client.stdin.write("LOGIN|Ana\n")
            client.stdin.flush()
            with self.connect() as idle_client:
                time.sleep(0.35)
                self.server.send_signal(signal.SIGTERM)
                self.assertEqual(idle_client.recv(4096), b"")
            self.server.wait(timeout=5)
            client.wait(timeout=4)
            self.assertEqual(client.returncode, 0)
            output = client.stdout.read()
            self.assertIn("Server closed the connection", output)
        finally:
            if client.poll() is None:
                client.kill()
                client.wait()
            client.stdin.close()
            client.stdout.close()
            client.stderr.close()

    def test_interrupt_closes_prelogin_connections(self):
        with self.connect() as idle_client:
            self.server.send_signal(signal.SIGINT)
            try:
                result = idle_client.recv(4096)
            except ConnectionResetError:
                result = b""  # O encerramento de TCP também pode gerar RST.
            self.assertEqual(result, b"")
            self.server.wait(timeout=5)
            self.assertEqual(self.server.returncode, 0)

    def test_broadcast_events_follow_accepted_bid_order(self):
        with self.connect() as observer:
            self.login(observer, "Observer")
            clients = [self.connect() for _ in range(10)]
            try:
                for i, connection in enumerate(clients):
                    self.login(connection, f"Bidder{i}")

                barrier = threading.Barrier(len(clients) + 1)
                results = [None] * len(clients)
                errors = []

                def place_bid(i, connection):
                    try:
                        barrier.wait(timeout=4)
                        connection.sendall(f"BID|{1100 + i}\n".encode("ascii"))
                        while True:
                            response = read_line(connection)
                            if response is None:
                                raise AssertionError("Conexão encerrada antes da resposta")
                            if response.startswith(("BID_ACCEPTED|", "BID_REJECTED|")):
                                results[i] = response
                                return
                    except Exception as exc:
                        errors.append(exc)

                threads = [
                    threading.Thread(target=place_bid, args=(i, connection))
                    for i, connection in enumerate(clients)
                ]
                for thread in threads:
                    thread.start()
                barrier.wait(timeout=4)
                for thread in threads:
                    thread.join(timeout=5)
                self.assertFalse(any(t.is_alive() for t in threads))
                self.assertEqual(errors, [])
                self.assertTrue(all(result is not None for result in results))

                accepted = [
                    int(result.split("|")[1]) for result in results
                    if result.startswith("BID_ACCEPTED|")
                ]
                observer.settimeout(0.5)
                observed = []
                while True:
                    try:
                        event = read_line(observer)
                    except socket.timeout:
                        break
                    if event is None:
                        break
                    self.assertTrue(event.startswith("EVENT|NEW_BID|"), event)
                    observed.append(int(event.split("|")[2]))

                self.assertEqual(len(observed), len(accepted))
                self.assertEqual(observed, sorted(observed))
                self.assertEqual(set(observed), set(accepted))

                with self.connect() as verifier:
                    verifier.sendall(b"STATUS\n")
                    self.assertEqual(read_line(verifier),
                                     "AUCTION|Notebook|1109|Bidder9")
            finally:
                for connection in clients:
                    connection.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
