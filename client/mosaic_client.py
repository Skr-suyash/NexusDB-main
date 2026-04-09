#!/usr/bin/env python3
"""MosaicDB Python Client - connects to MosaicDB TCP server."""

import socket
import struct
import sys

PROTO_PUT = 0x01
PROTO_GET = 0x02
PROTO_DELETE = 0x03
PROTO_SCAN = 0x04
RESP_OK = 0x00
RESP_NOT_FOUND = 0x01
RESP_ERROR = 0x02

class MosaicDBClient:
    def __init__(self, host="127.0.0.1", port=7690):
        self.host = host
        self.port = port
        self.sock = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def _send_all(self, data):
        self.sock.sendall(data)

    def _recv_all(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("Connection closed")
            buf += chunk
        return buf

    def _request(self, cmd_type, key, value=b""):
        if isinstance(key, str):
            key = key.encode("utf-8")
        if isinstance(value, str):
            value = value.encode("utf-8")

        header = struct.pack("<BII", cmd_type, len(key), len(value))
        self._send_all(header + key + value)

        resp_header = self._recv_all(5)
        status, val_size = struct.unpack("<BI", resp_header)
        resp_value = self._recv_all(val_size) if val_size > 0 else b""
        return status, resp_value

    def put(self, key, value):
        status, _ = self._request(PROTO_PUT, key, value)
        return status == RESP_OK

    def get(self, key):
        status, value = self._request(PROTO_GET, key)
        if status == RESP_OK:
            return value.decode("utf-8")
        elif status == RESP_NOT_FOUND:
            return None
        else:
            raise RuntimeError("Server error")

    def delete(self, key):
        status, _ = self._request(PROTO_DELETE, key)
        return status == RESP_OK

    def scan(self, start_key, end_key):
        status, value = self._request(PROTO_SCAN, start_key, end_key)
        if status != RESP_OK:
            raise RuntimeError("Server error on scan")
        
        if len(value) < 4:
            return []
            
        count, = struct.unpack("<I", value[:4])
        pos = 4
        result = []
        for _ in range(count):
            if pos + 8 > len(value):
                break
            ks, vs = struct.unpack("<II", value[pos:pos+8])
            pos += 8
            k = value[pos:pos+ks].decode("utf-8")
            pos += ks
            v = value[pos:pos+vs].decode("utf-8")
            pos += vs
            result.append((k, v))
        return result

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.close()


def interactive_mode(host, port):
    print(f"MosaicDB Client - connecting to {host}:{port}")
    try:
        with MosaicDBClient(host, port) as client:
            print("Connected. Commands: PUT key value | GET key | DELETE key | quit")
            while True:
                try:
                    line = input("MosaicDB> ").strip()
                except (EOFError, KeyboardInterrupt):
                    break
                if not line:
                    continue
                if line.lower() in ("quit", "exit"):
                    break

                parts = line.split(None, 2)
                cmd = parts[0].upper()

                if cmd == "PUT" and len(parts) >= 3:
                    ok = client.put(parts[1], parts[2])
                    print("OK" if ok else "ERROR")
                elif cmd == "GET" and len(parts) >= 2:
                    val = client.get(parts[1])
                    print(val if val is not None else "NOT_FOUND")
                elif cmd == "DELETE" and len(parts) >= 2:
                    ok = client.delete(parts[1])
                    print("OK" if ok else "ERROR")
                elif cmd == "SCAN" and len(parts) >= 3:
                    res = client.scan(parts[1], parts[2])
                    for k, v in res:
                        print(f"{k} => {v}")
                else:
                    print("Usage: PUT key value | GET key | DELETE key | SCAN start end")
    except ConnectionRefusedError:
        print(f"Error: Cannot connect to {host}:{port}")
        sys.exit(1)

    print("Goodbye.")


if __name__ == "__main__":
    host = "127.0.0.1"
    port = 7690

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--host" and i + 1 < len(args):
            host = args[i + 1]; i += 2
        elif args[i] == "--port" and i + 1 < len(args):
            port = int(args[i + 1]); i += 2
        else:
            i += 1

    interactive_mode(host, port)
