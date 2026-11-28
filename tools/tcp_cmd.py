#!/usr/bin/env python3
"""Send one line command to the snesrecomp debug TCP server.

The current server replies with one JSON line per command and does not
send a greeting. This helper prints that response directly so callers can
pipe it into jq/ConvertFrom-Json.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import os
import sys
sys.path.insert(0, str(Path(os.environ.get('SNESRECOMP_ROOT', Path(__file__).resolve().parents[1] / 'snesrecomp')) / 'tools'))
from sneslib.client import DebugClient, GameplayClient
import sys
import time


def send_command(host: str, port: int, command: str, timeout: float) -> list[str]:
    with DebugClient(port, host=host, timeout=timeout) as client:
        return [client.query_raw(command, retry=False)]


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
