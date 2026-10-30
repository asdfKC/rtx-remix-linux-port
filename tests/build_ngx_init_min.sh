#!/usr/bin/env bash
# Builds tests/ngx_init_min.c against the same NGX SDK headers and static loader
# (libnvsdk_ngx.a) dxvk-remix links, so the repro uses the exact same NGX entry point.
set -euo pipefail
WORK="${1:-/mnt/big/rtx-remix-linux}"
DLSS="$WORK/dxvk-remix/submodules/dlss"
OUT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/ngx_init_min"

gcc -O0 -g -o "$OUT" "$(dirname "${BASH_SOURCE[0]}")/ngx_init_min.c" \
  -I"$DLSS/include" -I"$WORK/dxvk-remix/include/vulkan/include" \
  "$DLSS/lib/Linux_x86_64/libnvsdk_ngx.a" \
  -lvulkan -ldl -lstdc++ -lm

echo "built: $OUT"
