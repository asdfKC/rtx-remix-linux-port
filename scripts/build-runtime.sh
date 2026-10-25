#!/usr/bin/env bash
# Builds the native Linux RTX Remix runtime (libremix.so) from public sources plus this repo's patches.
#
# Usage: scripts/build-runtime.sh [WORK_DIR]
#   WORK_DIR defaults to /mnt/big/rtx-remix-linux (keeps the root filesystem free; needs ~15 GB).
#
# Requires only official Arch packages:
#   sudo pacman -S --needed base-devel git cmake ninja meson python glslang spirv-tools vulkan-headers vulkan-icd-loader
#
# Environment:
#   REMIX_FONTS=packman   fetch rtx-remix-fonts@2 with dxvk-remix's own Packman script (downloads NVIDIA's Packman
#                         bootstrap Python and the font package from NVIDIA's CDN)
#   REMIX_FONTS=system    (default) use DejaVu fonts from the official ttf-dejavu package as stand-ins
#   JOBS=N                parallel build jobs (default: nproc)
set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${1:-/mnt/big/rtx-remix-linux}"
JOBS="${JOBS:-$(nproc)}"
REMIX_FONTS="${REMIX_FONTS:-system}"

# Pins (see PINS.md).
DXVK_REMIX_COMMIT=bbd74a1287c2fa887f4a2b0e46a1530177890b64
SLANG_TAG=v2025.10.4
NRD_COMMIT=3edea46dafb29ce05c61cf839b396b291fdaddb6

mkdir -p "$WORK"
cd "$WORK"

log() { printf '\n==> %s\n' "$*"; }

# --- slang ------------------------------------------------------------------
if [ ! -x slang/build/Release/bin/slangc ]; then
  log "slang $SLANG_TAG (+ patches/slang)"
  [ -d slang ] || git clone --filter=blob:none https://github.com/shader-slang/slang slang
  git -C slang checkout -q "$SLANG_TAG"
  git -C slang submodule update --init --recursive --depth 1
  git -C slang -c user.name=build -c user.email=build@localhost am -q "$PORT_DIR"/patches/slang/*.patch
  cmake -S slang -B slang/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSLANG_ENABLE_PREBUILT_BINARIES=OFF -DSLANG_SLANG_LLVM_FLAVOR=DISABLE \
    -DSLANG_ENABLE_GFX=OFF -DSLANG_ENABLE_SLANG_RHI=OFF -DSLANG_ENABLE_TESTS=OFF \
    -DSLANG_ENABLE_EXAMPLES=OFF -DSLANG_ENABLE_SLANGD=OFF -DSLANG_ENABLE_SLANGI=OFF \
    -DSLANG_ENABLE_REPLAYER=OFF -DSLANG_ENABLE_DXIL=OFF -DSLANG_ENABLE_CUDA=OFF \
    -DSLANG_ENABLE_OPTIX=OFF -DSLANG_ENABLE_AFTERMATH=OFF
  ninja -C slang/build -j"$JOBS" slangc slang-glslang
fi

# --- NRD headers and shaders (v4.13.0) --------------------------------------
if [ ! -d NRD ]; then
  log "NRD $NRD_COMMIT"
  git clone --filter=blob:none --no-checkout https://github.com/NVIDIA-RTX/NRD NRD
  git -C NRD checkout -q "$NRD_COMMIT"
fi

# --- dxvk-remix -------------------------------------------------------------
if [ ! -d dxvk-remix ]; then
  log "dxvk-remix $DXVK_REMIX_COMMIT (+ patches/dxvk-remix)"
  git clone --filter=blob:none --no-checkout https://github.com/NVIDIAGameWorks/dxvk-remix dxvk-remix
  git -C dxvk-remix checkout -q -b linux-port "$DXVK_REMIX_COMMIT"
  git -C dxvk-remix submodule update --init --recursive --depth 1
  git -C dxvk-remix -c user.name=build -c user.email=build@localhost am -q "$PORT_DIR"/patches/dxvk-remix/*.patch
fi

R=dxvk-remix
mkdir -p "$R/external/nrd" "$R/external/fonts/NVIDIASans" "$R/external/fonts/RobotoMono"
ln -sfn "$WORK/slang/build/Release/bin" "$R/external/slang"
ln -sfn "$WORK/NRD/Include" "$R/external/nrd/Include"
ln -sfn "$WORK/NRD/Shaders" "$R/external/nrd/Shaders"

if [ "$REMIX_FONTS" = packman ]; then
  log "fonts via Packman (rtx-remix-fonts@2)"
  cat > "$R/packman-linux-data.xml" <<'EOF'
<project toolsVersion="7.20">
    <dependency name="fonts" linkPath="external/fonts">
        <package name="rtx-remix-fonts" version="2" />
    </dependency>
</project>
EOF
  rm -rf "$R/external/fonts"
  (cd "$R" && bash scripts-common/packman/packman pull packman-linux-data.xml)
else
  log "fonts: DejaVu stand-ins (pacman -S ttf-dejavu)"
  D=/usr/share/fonts/TTF
  cp "$D/DejaVuSans.ttf"      "$R/external/fonts/NVIDIASans/NVIDIASansMd.ttf"
  cp "$D/DejaVuSans-Bold.ttf" "$R/external/fonts/NVIDIASans/NVIDIASansBd.ttf"
  cp "$D/DejaVuSansMono.ttf"  "$R/external/fonts/RobotoMono/RobotoMonoRg.ttf"
fi

# --- configure and build ----------------------------------------------------
log "meson configure"
[ -d build ] || meson setup build "$R" --buildtype=release -Dwerror=false -Denable_rtxio=false
log "ninja"
ninja -C build -j"$JOBS"

log "done: $WORK/build/src/d3d9/libremix.so"
