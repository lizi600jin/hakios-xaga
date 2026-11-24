#!/usr/bin/env python3
"""haki_cli - drive the phone on the other end of the USB link.

    haki_cli.py ping
    haki_cli.py exec "<command>"
    haki_cli.py push <local> [remote]
    haki_cli.py pull <remote> <local>
    haki_cli.py ls [path]
    haki_cli.py return

The device runs `hakid`, which answers one request per line on TCP port 2222 over the
CDC-ECM link. Only the standard library is used, so this works on a bare machine.

The interface address is set here rather than by hand: the link comes and goes with every
reboot, and a stale address is the most common reason a session fails.
"""

import argparse
import hashlib
import os
import socket
import subprocess
import sys
import time

DEFAULT_IFACE = "enx020000006801"
DEFAULT_HOST = "10.0.0.2"
DEFAULT_PORT = 2222
HOST_ADDRESS = "10.0.0.1/24"


def configure_interface(iface: str, quiet: bool = False) -> bool:
    """Give the host end of the link its address, and report whether the phone answers."""
    if not os.path.exists(f"/sys/class/net/{iface}"):
        if not quiet:
            print(f"  {iface} is not present", file=sys.stderr)
        return False
    # replace is idempotent and restores the address after the link has been re-created
    subprocess.run(["sudo", "-n", "ip", "addr", "replace", HOST_ADDRESS, "dev", iface],
                   capture_output=True)
    return True


def connect(host: str, port: int, timeout: float = 5.0) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return sock


def read_line(sock: socket.socket) -> str:
    data = b""
    while not data.endswith(b"\n"):
        piece = sock.recv(1)
        if not piece:
            break
        data += piece
    return data.decode("utf-8", "replace").rstrip("\n")


def do_ping(args) -> int:
    started = time.time()
    with connect(args.host, args.port) as sock:
        banner = read_line(sock)          # "HAKID 1 port ... root ..."
        sock.sendall(b"PING\n")
        answer = read_line(sock)
        elapsed = (time.time() - started) * 1000
        print(f"  {banner}")
        print(f"  {answer}   ({elapsed:.1f} ms round trip)")
    return 0


def do_exec(args) -> int:
    with connect(args.host, args.port) as sock:
        read_line(sock)
        sock.sendall(f"EXEC {args.command}\n".encode())
        status = read_line(sock)
        if not status.startswith("OK"):
            print(f"  {status}", file=sys.stderr)
            return 1
        streamed = b""
        while True:
            piece = sock.recv(16384)
            if not piece:
                break
            streamed += piece
            sys.stdout.write(piece.decode("utf-8", "replace"))
            sys.stdout.flush()
        # the last line is "EXIT n"
        text = streamed.decode("utf-8", "replace")
        code = 0
        for line in text.strip().split("\n")[::-1]:
            if line.startswith("EXIT "):
                code = int(line[5:].strip() or "-1")
                break
        return code


def do_push(args) -> int:
    local = args.local
    remote = args.remote or os.path.basename(local)
    with open(local, "rb") as handle:
        body = handle.read()
    digest = hashlib.sha256(body).hexdigest()

    with connect(args.host, args.port) as sock:
        read_line(sock)
        sock.sendall(f"PUT {remote} {len(body)} {digest}\n".encode())
        answer = read_line(sock)
        if answer != "OK send":
            print(f"  {answer}", file=sys.stderr)
            return 1
        sock.sendall(body)
        result = read_line(sock)
        print(f"  {result}")
        return 0 if result.startswith("OK") else 1


def do_pull(args) -> int:
    with connect(args.host, args.port) as sock:
        read_line(sock)
        sock.sendall(f"GET {args.remote}\n".encode())
        answer = read_line(sock)
        if not answer.startswith("OK "):
            print(f"  {answer}", file=sys.stderr)
            return 1
        size = int(answer[3:].strip())
        received = b""
        while len(received) < size:
            piece = sock.recv(min(16384, size - len(received)))
            if not piece:
                break
            received += piece
    with open(args.local, "wb") as handle:
        handle.write(received)
    print(f"  {len(received)} bytes -> {args.local}")
    return 0 if len(received) == size else 1


def do_ls(args) -> int:
    with connect(args.host, args.port) as sock:
        read_line(sock)
        sock.sendall(f"LS {args.path}\n".encode())
        while True:
            line = read_line(sock)
            if line in ("END", ""):
                break
            print(f"  {line}")
    return 0


def do_return(args) -> int:
    with connect(args.host, args.port) as sock:
        read_line(sock)
        sock.sendall(b"RETURN\n")
        print(f"  {read_line(sock)}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="drive the phone over the USB link")
    parser.add_argument("--iface", default=DEFAULT_IFACE)
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--no-setup", action="store_true",
                        help="do not touch the host interface address")
    sub = parser.add_subparsers(dest="action", required=True)

    sub.add_parser("ping").set_defaults(func=do_ping)

    exec_parser = sub.add_parser("exec")
    exec_parser.add_argument("command")
    exec_parser.set_defaults(func=do_exec)

    push_parser = sub.add_parser("push")
    push_parser.add_argument("local")
    push_parser.add_argument("remote", nargs="?")
    push_parser.set_defaults(func=do_push)

    pull_parser = sub.add_parser("pull")
    pull_parser.add_argument("remote")
    pull_parser.add_argument("local")
    pull_parser.set_defaults(func=do_pull)

    ls_parser = sub.add_parser("ls")
    ls_parser.add_argument("path", nargs="?", default="/")
    ls_parser.set_defaults(func=do_ls)

    sub.add_parser("return").set_defaults(func=do_return)

    args = parser.parse_args()
    if not args.no_setup:
        configure_interface(args.iface)
    try:
        return args.func(args)
    except (ConnectionRefusedError, socket.timeout, OSError) as error:
        print(f"  cannot reach {args.host}:{args.port}: {error}", file=sys.stderr)
        print("  (is the phone in XNU, past the 24 s mark, with hakid running?)", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
