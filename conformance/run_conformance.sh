#!/usr/bin/env bash
# Smart2Raw conformance runner
# Copyright (C) 2026 Carlos Alberto Terencio Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
python3 conformance/scripts/generate_fixtures.py
make -C tools >/tmp/s2r_tools_build.log
for f in conformance/fixtures/*.s2r; do
  case "$f" in
    *corrupted_crc.s2r) continue ;;
  esac
  tools/s2r verify "$f" >/dev/null
  tools/s2r agg "$f" sum >/dev/null
done
if tools/s2r verify conformance/fixtures/corrupted_crc.s2r >/dev/null 2>&1; then
  echo "corrupted fixture unexpectedly passed C verifier" >&2
  exit 1
fi
PYTHONPATH=ports/python python3 -m unittest discover -s ports/python/tests
(cd ports/go && go test ./...)
(cd ports/js && node --test smart2raw.test.mjs)
echo "conformance: OK"
