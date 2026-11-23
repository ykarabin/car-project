#!/usr/bin/env python3
"""Run one Tcl command in a running OpenOCD (Tcl RPC on 127.0.0.1:6666).

Example: openocd_rpc.py "mdw 0x0807F000 8"
Prints the result; exit code 1 if the command failed.
"""
import socket
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    command = sys.argv[1]
    request = f"set rpc_status [catch {{{command}}} rpc_result]; list $rpc_status $rpc_result\x1a"
    data = bytearray()
    with socket.create_connection(("127.0.0.1", 6666), timeout=5) as sock:
        sock.settimeout(180)
        sock.sendall(request.encode())
        while not data.endswith(b"\x1a"):
            chunk = sock.recv(65536)
            if not chunk:
                raise RuntimeError("OpenOCD disconnected")
            data.extend(chunk)
    text = data[:-1].decode(errors="replace")
    print(text)
    return 0 if text.startswith("0 ") else 1


if __name__ == "__main__":
    sys.exit(main())
