#!/usr/bin/env bash
# Smart2Raw
# Copyright (C) 2026 Carlos Alberto Terêncio de Bastos
# SPDX-License-Identifier: AGPL-3.0-or-later
# Updates the LICENSE file with the verbatim AGPL-3.0 text from gnu.org.
set -e
URL="https://www.gnu.org/licenses/agpl-3.0.txt"
cd "$(dirname "$0")/.."
echo "Downloading $URL ..."
curl -fL "$URL" -o LICENSE
printf 'LICENSE updated with the verbatim AGPL-3.0 text.\n'
