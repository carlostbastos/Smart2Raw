#!/usr/bin/env bash
# Smart2Raw
# Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
# Reproduces the test suite and the measured benchmarks.
set -e
cd "$(dirname "$0")/.."
echo "### Tests ###"; bash scripts/build_and_test.sh
echo; echo "### Benchmark: cache cliff ###"
gcc -O3 -march=native -o /tmp/cliff benchmarks/cliff.c && /tmp/cliff || true
echo; echo "### Benchmark: AI application (PFOR + zero-point) ###"
gcc -O3 -march=native -I include -o /tmp/ai benchmarks/ai_explore.c && /tmp/ai
echo; echo "Machine/compiler used are in benchmarks/RESULTS.md"
