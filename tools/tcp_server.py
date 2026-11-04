"""Minimal TCP echo/ACK server for the ESP32 LAN communication test."""

from __future__ import annotations

import socket
import threading


HOST = "0.0.0.0"
PORT = 5000
ACK = b"ACK from PC\n"
CLIENT_IDLE_TIMEOUT_SECONDS = 30


def serve_client(connection: socket.socket, address: tuple[str, int]) -> None:
    print(f"client connected: {address[0]}:{address[1]}", flush=True)
    with connection:
        # ESP32 sends a line every five seconds.  A much longer timeout lets
        # us clean up a connection that became stale without affecting the
        # normal test traffic.
        connection.settimeout(CLIENT_IDLE_TIMEOUT_SECONDS)
        buffer = b""
        while True:
            try:
                data = connection.recv(1024)
            except socket.timeout:
                print(
                    f"client idle timeout: {address[0]}:{address[1]}",
                    flush=True,
                )
                return
            except ConnectionResetError:
                print(f"client reset connection: {address[0]}:{address[1]}", flush=True)
                return
            if not data:
                print(f"client disconnected: {address[0]}:{address[1]}", flush=True)
                return
            buffer += data
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                message = line.decode("utf-8", errors="replace")
                print(f"received: {message}", flush=True)
                try:
                    connection.sendall(ACK)
                except (BrokenPipeError, ConnectionResetError):
                    print(f"client reset connection: {address[0]}:{address[1]}", flush=True)
                    return
                print("sent: ACK from PC", flush=True)


def main() -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(8)
        print(f"TCP server listening on {HOST}:{PORT}", flush=True)
        while True:
            connection, address = server.accept()
            # Handle each connection independently.  If the ESP32 is reset,
            # its old connection can linger briefly; a new connection must
            # still be accepted immediately instead of waiting for the old
            # one to finish.
            client_thread = threading.Thread(
                target=serve_client,
                args=(connection, address),
                daemon=True,
            )
            client_thread.start()


if __name__ == "__main__":
    main()
