#!/usr/bin/env bash
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MORPHOS_WORKDIR="${ROOT_DIR}" exec "${ROOT_DIR}/../morphos-cross/bin/morphos-run" \
    make -f Makefile.mosprefs "$@"
