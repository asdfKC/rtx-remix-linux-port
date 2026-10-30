# Quality Pass Status

Branch: `quality-pass` (from `live-test-fold`). Unattended run, started 2026-10-29.

## Phase 0: Baseline — DONE (measured)

Fixed view: Creative world "New World", coords `0.5 80.0 0.5`, facing south (yaw 0 pitch 0),
daytime (`/time set day`, daylight cycle off), clear weather forced for 100000 ticks, weather
cycle off.

Measured:
- F3: **77 fps**, vsync target 120 ("fabulous"), **GPU 56%** (snapshot) / 67-70% sm util over
  15 `nvidia-smi dmon` samples, ~132W power draw (board is ~220W rated) — GPU is not saturated.
- Two native `dxvk-device` threads (tid 1781376, 1781377) at **94-99% CPU each** — this is the
  real bottleneck, not the GPU.
- 5x jstack of the Java "Render thread" 1s apart: 3/5 caught inside `glFinish()` at
  `GlInterop.beforeRemixPresent:169`, 2/5 inside the native `RemixBridge.present` call.
- Log confirms: `NVIDIA Turing architecture detected, setting default graphics settings to Low`
  (misdetection on the 5070/Blackwell), `Unable to load NRD, this feature will be disabled`,
  `NGX (DLSS) is off on Linux`.

**Conclusion driving Phase 2**: hypothesis (a) from the task brief is confirmed — the mod's
per-frame `glFinish()` in `GlInterop.beforeRemixPresent` is the dominant stall, serializing CPU
against GPU every frame and capping fps well below what the ~68% GPU utilization would allow.

Evidence: `evidence/quality-pass/baseline.txt`, `evidence/quality-pass/baseline_screenshot.png`.

## Phase 1: wrong graphics preset — DONE (verified on GPU)

Root cause confirmed: `RtxOptions::getNvidiaArch()` (`rtx_options.cpp`) hardcoded
`NV_GPU_ARCHITECTURE_TU100` whenever `NvAPI_Initialize()` fails, which it always does on Linux
(no NvAPI at all — see `linux/stubs/remix_linux_stubs.cpp`). This forced every non-Turing GPU,
including this port's RTX 5070 (Blackwell, GB205, PCI deviceID `0x2f04`), onto Low settings.

Fix (runtime patch `patches/dxvk-remix/0013-...patch`): added a Vulkan-deviceID-range fallback
table (Turing/Ampere/Ada/Blackwell), consulted only when NVAPI is unavailable, fed from the
`VkPhysicalDeviceProperties` already in scope at the `updateGraphicsPresets()` call site. No
change to the existing Ada→High / Blackwell→Ultra preset tier mapping — only the broken
architecture *input* was fixed.

Verified on GPU: log changed from `NVIDIA Turing architecture detected, setting default graphics
settings to Low` to `NVAPI unavailable; using Vulkan deviceID 0x2f04 to guess GPU architecture` →
`NVIDIA Blackwell architecture detected, setting default graphics settings to Ultra`. Same fixed
view (0.5, 80.0, 0.5, facing south, day, clear):
- Low (pre-fix, misdetected): 77 fps, GPU sm util 56-70%
- High (forced via env var): 65 fps, GPU sm util ~52%
- **Ultra (post-fix, correct)**: 58 fps, GPU sm util ~71-75%, power ~137-146W

GPU is still not saturated at Ultra — consistent with Phase 0's finding that the Render thread is
CPU-bound on `glFinish()`, not GPU-bound. Evidence: `evidence/quality-pass/phase1.txt`,
`preset_high_screenshot.png`, `phase1_fixed_screenshot.png`.

## Phase 2: per-frame stall (glFinish) — NOT STARTED
Baseline evidence already points at hypothesis (a): the Render thread spends most of its time in
`glFinish()` (`GlInterop.beforeRemixPresent`) and the native `RemixBridge.present` call. Next:
replace `glFinish` with a real semaphore wait or ring-buffered interop image.

## Phase 3: NRD denoiser — NOT STARTED

## Phase 4: DLSS investigation — NOT STARTED

## Next step
Phase 2 (per-frame glFinish stall) — highest value remaining, per the Phase 0 jstack evidence.
