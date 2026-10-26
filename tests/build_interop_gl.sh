#!/bin/sh
# Builds tests/interop_gl.c against system EGL/GL (mesa/libglvnd, official packages) and the
# dxvk-remix public header for remix_c.h.
#
# Usage: tests/build_interop_gl.sh <path to dxvk-remix checkout> [output binary]
set -e
DXVK_REMIX="${1:-/mnt/big/rtx-remix-linux/dxvk-remix}"
OUT="${2:-/mnt/big/remix-test/interop_gl}"
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"

cc -O2 -g -Wall \
  -I"${DXVK_REMIX}/public/include" \
  "${SELF_DIR}/interop_gl.c" \
  -ldl -lEGL -lGL -lm \
  -o "${OUT}"

echo "built: ${OUT}"
