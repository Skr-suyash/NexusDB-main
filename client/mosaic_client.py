#!/usr/bin/env python3
"""MosaicDB Python Client - connects to MosaicDB TCP server.
Supports both legacy KV commands and SQL queries."""

import socket
import struct
import sys

PROTO_PUT = 0x01
PROTO_GET = 0x02
PROTO_DELETE = 0x03
PROTO_SCAN = 0x04
PROTO_SQL = 0x05
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

    # ── Legacy KV Commands ──────────────────────────────────────

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

    # ── SQL Commands ────────────────────────────────────────────

    def execute_sql(self, sql):
        """Execute a SQL query on the server. Returns a dict with:
        - 'ok': bool
        - 'message': str
        - 'columns': list of column name strings
        - 'rows': list of list of value strings
        """
        status, value = self._request(PROTO_SQL, sql)
        result = {
            "ok": status == RESP_OK,
            "message": "",
            "columns": [],
            "rows": [],
        }

        if len(value) < 4:
            result["message"] = value.decode("utf-8", errors="replace") if value else "No response"
            return result

        pos = 0

        def read_u32():
            nonlocal pos
            v, = struct.unpack("<I", value[pos:pos+4])
            pos += 4
            return v

        def read_str(length):
            nonlocal pos
            s = value[pos:pos+length].decode("utf-8", errors="replace")
            pos += length
            return s

        # Message
        msg_len = read_u32()
        result["message"] = read_str(msg_len)

        # Column names
        col_count = read_u32()
        for _ in range(col_count):
            clen = read_u32()
            result["columns"].append(read_str(clen))

        # Rows
        row_count = read_u32()
        for _ in range(row_count):
            row = []
            for _ in range(col_count):
                vlen = read_u32()
                row.append(read_str(vlen))
            result["rows"].append(row)

        return result

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.close()


def print_table(columns, rows):
    """Print a formatted ASCII table."""
    if not columns:
        return

    widths = [len(c) for c in columns]
    for row in rows:
        for i, val in enumerate(row):
            if i < len(widths):
                widths[i] = max(widths[i], len(str(val)))

    sep = "+" + "+".join("-" * (w + 2) for w in widths) + "+"

    print(sep)
    print("|" + "|".join(f" {c:<{widths[i]}} " for i, c in enumerate(columns)) + "|")
    print(sep)
    for row in rows:
        print("|" + "|".join(f" {str(row[i]) if i < len(row) else '':<{widths[i]}} " for i in range(len(columns))) + "|")
    print(sep)


def interactive_mode(host, port):
    print(f"MosaicDB SQL Client - connecting to {host}:{port}")
    try:
        with MosaicDBClient(host, port) as client:
            print("Connected. Enter SQL queries or 'quit' to exit.")
            print("Examples: CREATE TABLE, INSERT INTO, SELECT, UPDATE, DELETE, SHOW TABLES")
            print()
            while True:
                try:
                    line = input("MosaicSQL> ").strip()
                except (EOFError, KeyboardInterrupt):
                    break
                if not line:
                    continue
                if line.lower() in ("quit", "exit"):
                    break

                result = client.execute_sql(line)

                if not result["ok"]:
                    print(f"ERROR: {result['message']}")
                elif result["columns"] and result["rows"]:
                    print_table(result["columns"], result["rows"])
                    print(result["message"])
                elif result["columns"] and not result["rows"]:
                    print_table(result["columns"], [])
                    print("0 row(s) returned.")
                else:
                    print(result["message"])

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
