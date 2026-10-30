=== Phase 3: NRD denoiser — BLOCKED on package install (needs sudo password) ===

Research findings (background agent):
- dxvk-remix dlopen()s libNRD.so from the runtime's own directory on Linux
  (rtx_nrd_context.cpp:76-84), resolves 9 symbols (CreateInstance, GetLibraryDesc, etc.),
  and version-checks against NRD_VERSION_MAJOR/MINOR/BUILD from NRD.h.
- dxvk-remix/external/nrd/{Include,Shaders} are symlinks straight into this machine's
  /mnt/big/rtx-remix-linux/NRD checkout (commit 3edea46, v4.13.0), so there is zero version
  drift risk once libNRD.so is built from that same checkout -- the header the runtime
  compiles against and the header NRD builds against are the literal same file.
- NRD has not been built anywhere on this machine. Its CMakeLists.txt is already
  Linux/GCC/Clang-aware (no Windows-only blockers), small (Source/ 424K, Shaders/ 1.7M),
  and only needs two FetchContent deps (MathLib, ShaderMake) plus a working DXC binary with
  SPIR-V output support (DXC_SPIRV_PATH) -- D3D11/D3D12 shader embedding is auto-disabled on
  non-Windows, so no extra flags are needed to keep this Vulkan-only.

Blocker: the Arch package that provides DXC -- extra/directx-shader-compiler -- is not
installed, and installing it requires 'sudo pacman -S directx-shader-compiler', which needs
an interactive password prompt this unattended session cannot provide ('sudo: a password is
required', confirmed with both 'sudo -n true' and a direct install attempt).

This is an official Arch repo package (not AUR), so installing it is in-policy; it just needs
the user to run one command once, interactively:

  sudo pacman -S directx-shader-compiler

After that, run (adjust DXC_SPIRV_PATH if the installed binary lands somewhere other than
/usr/bin/dxc -- verify with 'pacman -Ql directx-shader-compiler | grep bin/'):

  cd /mnt/big/rtx-remix-linux/NRD
  cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DNRD_STATIC_LIBRARY=OFF \
    -DNRD_DISABLE_SHADER_COMPILATION=OFF \
    -DNRD_EMBEDS_SPIRV_SHADERS=ON \
    -DDXC_SPIRV_PATH=/usr/bin/dxc
  cmake --build build --parallel
  find build -iname 'libNRD.so'

Once libNRD.so exists, the remaining work (not yet done, next session):
  1. Copy it next to libremix.so -- the dir dxvk::env::getDllDirectory() resolves to inside
     remix-sdk/ in the Prism instance (same place libremix.so/libremixbridge.so are staged).
  2. Update scripts/build-runtime.sh and the mod's stageLinuxRuntime Gradle task to copy
     libNRD.so alongside the other runtime libraries so it ships with the jar automatically.
  3. Relaunch, confirm the log no longer says 'Unable to load NRD', screenshot before/after
     at the fixed view to show grain reduced, note the FPS cost.

No runtime code changes are needed for NRD loading itself -- rtx_nrd_context.cpp already
does the right dlopen()/symbol-resolution/version-check dance; this is purely a missing build
artifact, not a bug. No patch filed for Phase 3; stopping here per budget rules rather than
working around the missing credential.
