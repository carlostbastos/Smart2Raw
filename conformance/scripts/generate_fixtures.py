#!/usr/bin/env python3
# Smart2Raw conformance fixture generator
# Copyright (C) 2026 Carlos Alberto Terencio Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "ports" / "python"))

from smart2raw import Smart2RawPool  # noqa: E402

FIXTURES = [
    {"name": "unsigned_u8", "signed": False, "values": [0, 1, 25, 255]},
    {"name": "unsigned_u16", "signed": False, "values": [0, 255, 256, 65535]},
    {"name": "unsigned_u32", "signed": False, "values": [0, 65536, 1000000]},
    {"name": "signed_i8", "signed": True, "values": [-128, -1, 0, 127]},
    {"name": "signed_i16", "signed": True, "values": [-129, 0, 32767]},
]


def main() -> int:
    out_dir = ROOT / "conformance" / "fixtures"
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest = []
    for spec in FIXTURES:
        pool = Smart2RawPool(spec["values"], signed=spec["signed"])
        filename = f"{spec['name']}.s2r"
        path = out_dir / filename
        pool.save(path)
        manifest.append(
            {
                "file": filename,
                "name": spec["name"],
                "signed": spec["signed"],
                "class": pool.size,
                "count": pool.count,
                "sum": pool.sum(),
                "values": list(pool.values),
            }
        )

    bad = bytearray((out_dir / "unsigned_u8.s2r").read_bytes())
    bad[16] ^= 0x7F
    (out_dir / "corrupted_crc.s2r").write_bytes(bad)

    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(manifest)} fixtures to {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
