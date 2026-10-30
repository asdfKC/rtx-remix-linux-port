# Job A: NRD denoiser — built, shipped, verified

## Summary

`libNRD.so` is built from `/mnt/big/rtx-remix-linux/NRD` (the same checkout dxvk-remix's
`external/nrd/{Include,Shaders}` symlinks point at, so there is no header/library version
drift), ships inside the mod jar automatically, and loads cleanly in the game. Verified by
screenshot and the remix-dxvk.log.

## What was blocking it (new finding this session)

`directx-shader-compiler` (dxc) is now installed, clearing the Phase 3 blocker. But NRD's
`CMakeLists.txt` points its two FetchContent dependencies (ShaderMake, MathLib) at the
floating `main` branch of each repo. Both have drifted since NRD v4.13.0 (Feb 2025) was
pinned here:

- ShaderMake's current `main` removed the `--useAPI` flag NRD's CMakeLists.txt still passes
  (`ninja: unknown option --useAPI`).
- MathLib's current `main` renamed/removed `float4x4::col2`, `GetRow0()`, `GetRow1()` that
  NRD's `InstanceImpl.cpp`/`Relax.cpp` still call (compile errors).

Fix: pinned both FetchContent deps to the commit each repo was at around NRD's Feb 2025 pin
date (ShaderMake `809711a`, MathLib `ce3142d`), and restored `--useAPI` (present at that
ShaderMake commit). Patch: `patches/nrd/0001-linux-pin-shadermake-mathlib-fetchcontent-deps.patch`,
applied by `scripts/build-runtime.sh` the same way `patches/dxvk-remix` and `patches/slang`
are.

## Build + ship

- `scripts/build-runtime.sh` now clones/patches NRD onto a `linux-port` branch (matching the
  dxvk-remix/slang pattern) and builds `NRD/build/libNRD.so` with cmake+ninja whenever `dxc`
  is on PATH, skipping cleanly with a log line if it isn't.
- `rtx-remix-minecraft`'s `stageLinuxRuntime` Gradle task now auto-detects
  `<work>/NRD/build/libNRD.so` (relative to the staged `libremix.so`, same pattern as the
  existing DLSS auto-lookup) and stages it into `remix-runtime/linux64`, which the `packageJar`
  task already bundles into `native/remix-sdk-linux64/` inside the mod jar. No new install.sh
  flag needed — it's automatic, like the runtime library itself.
  Patch: `patches/rtx-remix-minecraft/0007-linux-stage-libNRD.so-automatically-from-NRD-build.patch`.
- `NativeExtractor.java` (mod-side, unmodified) extracts the jar's bundled
  `native/remix-sdk-linux64/` tree into the instance's `remix-sdk/` on every launch,
  retiring the old directory first — this is why a manual `cp` into `remix-sdk/` didn't
  survive a relaunch earlier in this session, and why staging has to go through the jar.

## Verification

Fixed view (0.5, 80.0, 0.5, facing south, day, clear weather), same view as all prior phases.

- `remix-dxvk.log`: zero NRD-related lines (`evidence/nrd-dlss/nrd_log_grep.txt`) — before
  this fix it logged `Unable to load NRD, this feature will be disabled` 7 times. The loader
  (`rtx_nrd_context.cpp`) only logs on failure, so silence here means `dlopen()` + all 9
  symbol resolutions + the version check succeeded.
- `nm -D --defined-only libNRD.so` lists all 9 C symbols the runtime resolves (CreateInstance,
  DestroyInstance, GetComputeDispatches, GetDenoiserString, GetInstanceDesc, GetLibraryDesc,
  GetResourceTypeString, SetCommonSettings, SetDenoiserSettings).
- Visual: cropped before/after screenshots of the same pyramid structure
  (`nrd_before_crop_pyramid.png` / `nrd_after_crop_pyramid.png`) show clearly reduced grain/
  speckle in shadowed areas (pyramid steps, foreground wall shadows) with NRD active — the
  expected effect of a working denoiser on path-traced lighting.
- FPS/GPU cost at this view: before (Phase 2 baseline, NRD unavailable) 92 fps / GPU 95-100%;
  after (NRD active) 81 fps / GPU 82%. That's a real ~12% fps cost, and GPU utilization
  dropping alongside fps suggests the bottleneck partly shifted off the GPU (plausibly NRD's
  CPU-side per-frame dispatch-list construction) rather than being purely more GPU compute
  work. Not further profiled — out of scope for this session's budget.

## Files

- `evidence/nrd-dlss/nrd_after_screenshot.png` — full F3 overlay screenshot with NRD active
  (81 fps, GPU 82%, same XYZ/facing as the Phase 2 baseline screenshot).
- `evidence/nrd-dlss/nrd_before_crop_pyramid.png`, `nrd_after_crop_pyramid.png` — matched crops
  showing the grain reduction.
- `evidence/nrd-dlss/nrd_log_grep.txt` — grep of remix-dxvk.log for nrd/denoiser (empty, i.e.
  clean load).
