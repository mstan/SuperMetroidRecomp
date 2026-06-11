#!/usr/bin/env python3
"""Send one line command to the snesrecomp debug TCP server.

The current server replies with one JSON line per command and does not
send a greeting. This helper prints that response directly so callers can
pipe it into jq/ConvertFrom-Json.
"""
from __future__ import annotations

import argparse
import socket
import sys
import time


def recv_lines(sock: socket.socket, timeout: float, stop_after: int = 2) -> list[str]:
    deadline = time.monotonic() + timeout
    data = bytearray()
    lines: list[str] = []
    sock.setblocking(False)
    while time.monotonic() < deadline:
        try:
            chunk = sock.recv(65536)
        except BlockingIOError:
            time.sleep(0.01)
            continue
        if not chunk:
            break
        data.extend(chunk)
        while b"\n" in data:
            raw, _, rest = data.partition(b"\n")
            data = bytearray(rest)
            line = raw.decode("utf-8", errors="replace").strip()
            if line:
                lines.append(line)
            if len(lines) >= stop_after:
                return lines
    tail = data.decode("utf-8", errors="replace").strip()
    if tail:
        lines.append(tail)
    return lines


def send_command(host: str, port: int, command: str, timeout: float) -> list[str]:
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(command.encode("ascii") + b"\n")
        return recv_lines(sock, timeout, stop_after=1)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=4380)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--include-greeting", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    if not args.command:
        parser.error("missing command")
    command = " ".join(args.command)
    lines = send_command(args.host, args.port, command, args.timeout)
    if not args.include_greeting and lines and '"connected"' in lines[0]:
        lines = lines[1:]
    if lines:
        sys.stdout.write(lines[-1] + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
