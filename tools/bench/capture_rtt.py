#!/usr/bin/env python3
"""Save the ECU RTT stream to a file for a fixed time.

Needs OpenOCD running with firmware/vehicle-ecu/tools/rtt-console.cfg
(RTT server on 127.0.0.1:9090).

Usage: capture_rtt.py OUTPUT_FILE SECONDS
"""
import socket
import sys
import time
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    output = Path(sys.argv[1])
    end = time.monotonic() + float(sys.argv[2])
    with socket.create_connection(("127.0.0.1", 9090), timeout=5) as sock, \
            output.open("wb", buffering=0) as log:
        sock.settimeout(1)
        while time.monotonic() < end:
            try:
                data = sock.recv(65536)
            except socket.timeout:
                continue
            if not data:
                break
            log.write(data)
    print(f"RTT capture finished: {output} ({output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
