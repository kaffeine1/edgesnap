#!/usr/bin/env bash
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OS4_WORKDIR="${ROOT_DIR}" exec "${ROOT_DIR}/../os4-cross/bin/os4-run" \
    make -f Makefile.os4tool "$@"
