#!/usr/bin/env python3
"""Automated test for MosaicDB network client."""

import sys
sys.path.insert(0, "client")
from mosaic_client import MosaicDBClient

def main():
    passed = 0
    failed = 0

    with MosaicDBClient("127.0.0.1", 7691) as client:
        # Test PUT
        assert client.put("name", "MosaicDB"), "PUT failed"
        passed += 1; print("  [PASS] PUT")

        # Test GET
        val = client.get("name")
        assert val == "MosaicDB", f"GET returned {val}"
        passed += 1; print("  [PASS] GET")

        # Test overwrite
        client.put("name", "MosaicDB_v2")
        val = client.get("name")
        assert val == "MosaicDB_v2", f"Overwrite GET returned {val}"
        passed += 1; print("  [PASS] Overwrite")

        # Test DELETE
        assert client.delete("name"), "DELETE failed"
        val = client.get("name")
        assert val is None, f"Deleted key returned {val}"
        passed += 1; print("  [PASS] DELETE")

        # Test missing key
        val = client.get("nonexistent")
        assert val is None, f"Missing key returned {val}"
        passed += 1; print("  [PASS] Missing key")

        # Test bulk operations
        for i in range(100):
            client.put(f"bulk_{i}", f"value_{i}")
        for i in range(100):
            val = client.get(f"bulk_{i}")
            assert val == f"value_{i}", f"Bulk GET wrong for bulk_{i}"
        passed += 1; print("  [PASS] Bulk 100 PUT/GET")

        # Test SCAN
        client.put("scan_1", "AAA")
        client.put("scan_2", "BBB")
        client.put("scan_3", "CCC")
        client.put("scan_4", "DDD")
        res = client.scan("scan_2", "scan_3")
        assert len(res) == 2, f"Scan returned wrong size: {len(res)}"
        assert res[0] == ("scan_2", "BBB")
        assert res[1] == ("scan_3", "CCC")
        passed += 1; print("  [PASS] SCAN")

    print(f"\n{passed}/{passed + failed} network tests passed.")
    return 0 if failed == 0 else 1

if __name__ == "__main__":
    print("=== Network Client Tests ===")
    sys.exit(main())
