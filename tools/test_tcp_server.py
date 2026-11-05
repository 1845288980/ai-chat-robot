"""Local protocol regression tests for tcp_server.py."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import socket
import tempfile
import threading
import unittest
import wave


SERVER_PATH = Path(__file__).with_name("tcp_server.py")
SPEC = importlib.util.spec_from_file_location("tcp_server", SERVER_PATH)
assert SPEC is not None and SPEC.loader is not None
tcp_server = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tcp_server)


def recv_line(connection: socket.socket) -> bytes:
    result = bytearray()
    while not result.endswith(b"\n"):
        chunk = connection.recv(1)
        if not chunk:
            break
        result.extend(chunk)
    return bytes(result)


class TcpServerProtocolTest(unittest.TestCase):
    def test_text_ack_and_pcm_wav_share_connection(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ptt-test-") as temp_dir:
            original_dir = tcp_server.RECORDINGS_DIR
            tcp_server.RECORDINGS_DIR = Path(temp_dir)
            server_socket, client_socket = socket.socketpair()
            client_socket.settimeout(3)
            worker = threading.Thread(
                target=tcp_server.serve_client,
                args=(server_socket, ("local", 1)),
            )
            worker.start()
            try:
                client_socket.sendall(b"hello from ESP32\n")
                self.assertEqual(recv_line(client_socket), tcp_server.ACK)

                pcm = bytes(16000)
                header = tcp_server.PCM_HEADER.pack(b"PCM1", 16000, 1, 16, len(pcm))
                client_socket.sendall(header + pcm)
                self.assertTrue(recv_line(client_socket).startswith(b"SAVED "))

                wav_path, = Path(temp_dir).glob("*.wav")
                with wave.open(str(wav_path), "rb") as wav_file:
                    self.assertEqual(wav_file.getframerate(), 16000)
                    self.assertEqual(wav_file.getnchannels(), 1)
                    self.assertEqual(wav_file.getsampwidth(), 2)
                    self.assertEqual(wav_file.getnframes(), 8000)
            finally:
                client_socket.close()
                worker.join(timeout=3)
                tcp_server.RECORDINGS_DIR = original_dir
            self.assertFalse(worker.is_alive())


if __name__ == "__main__":
    unittest.main()
