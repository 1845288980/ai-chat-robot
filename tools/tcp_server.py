"""TCP ACK and PCM-to-WAV receiver for the ESP32 voice test."""

from __future__ import annotations

from datetime import datetime
import socket
import struct
import threading
import wave
from pathlib import Path


HOST = "0.0.0.0"
PORT = 5000
ACK = b"ACK from PC\n"
CLIENT_IDLE_TIMEOUT_SECONDS = 30
RECORDINGS_DIR = Path(__file__).resolve().parents[1] / "recordings"
PCM_HEADER = struct.Struct("!4sIHHI")
MAX_PCM_BYTES = 16000 * 2 * 10


def recv_exact(connection: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = connection.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("client disconnected during frame")
        chunks.extend(chunk)
    return bytes(chunks)


def serve_audio(connection: socket.socket, first_four: bytes) -> None:
    header = first_four + recv_exact(connection, PCM_HEADER.size - len(first_four))
    magic, sample_rate, channels, sample_width_bits, payload_length = PCM_HEADER.unpack(header)
    if (magic != b"PCM1" or sample_rate != 16000 or channels != 1 or
            sample_width_bits != 16 or payload_length == 0 or
            payload_length > MAX_PCM_BYTES or payload_length % 2 != 0):
        connection.sendall(b"ERROR invalid PCM frame\n")
        return
    pcm = recv_exact(connection, payload_length)
    RECORDINGS_DIR.mkdir(parents=True, exist_ok=True)
    filename = f"recording_{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}.wav"
    output_path = RECORDINGS_DIR / filename
    with wave.open(str(output_path), "wb") as wav_file:
        wav_file.setnchannels(channels)
        wav_file.setsampwidth(sample_width_bits // 8)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm)
    connection.sendall(f"SAVED {filename}\n".encode("ascii"))
    print(f"saved WAV: {output_path} ({payload_length} PCM bytes)", flush=True)


def serve_text(connection: socket.socket, first_four: bytes) -> None:
    buffer = bytearray(first_four)
    while b"\n" not in buffer:
        data = connection.recv(1024)
        if not data:
            return
        buffer.extend(data)
    line, _, _ = buffer.partition(b"\n")
    message = line.decode("utf-8", errors="replace")
    print(f"received: {message}", flush=True)
    connection.sendall(ACK)
    print("sent: ACK from PC", flush=True)


def serve_client(connection: socket.socket, address: tuple[str, int]) -> None:
    print(f"client connected: {address[0]}:{address[1]}", flush=True)
    with connection:
        connection.settimeout(CLIENT_IDLE_TIMEOUT_SECONDS)
        while True:
            try:
                first_four = recv_exact(connection, 4)
                if first_four == b"PCM1":
                    serve_audio(connection, first_four)
                else:
                    serve_text(connection, first_four)
            except socket.timeout:
                print(f"client idle timeout: {address[0]}:{address[1]}", flush=True)
                return
            except (ConnectionError, ConnectionResetError, BrokenPipeError) as exc:
                print(f"client disconnected/reset: {address[0]}:{address[1]} ({exc})", flush=True)
                return
            except (OSError, wave.Error) as exc:
                print(f"client error: {address[0]}:{address[1]} ({exc})", flush=True)
                try:
                    connection.sendall(b"ERROR save failed\n")
                except OSError:
                    return


def main() -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(8)
        print(f"TCP server listening on {HOST}:{PORT}", flush=True)
        while True:
            connection, address = server.accept()
            threading.Thread(
                target=serve_client,
                args=(connection, address),
                daemon=True,
            ).start()


if __name__ == "__main__":
    main()
