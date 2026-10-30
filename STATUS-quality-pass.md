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

## Phase 2: per-frame stall (glFinish) — DONE (verified on GPU, big win)

Root cause confirmed by background research: the `clientDone` binary semaphore was created and
exported to the mod via `dxvk_GetInteropInfo` (patch 0004/0010), but the runtime never waited on
it anywhere — `GlInterop.beforeRemixPresent()` worked around this with a per-frame `glFinish()`
(documented as a known gap in `KNOWN_GAPS.md`).

Fix (option (i) from the task brief — runtime waits on clientDone, no ring buffer needed):
- Runtime (`patches/dxvk-remix/0014-...patch`): `D3D9SwapchainExternal::Present()` now adds a
  wait on `m_interopClientDoneSemaphore` before each blit into `m_interopColorImage`, skipped only
  on the first blit of each image's lifetime (fresh image / after resize — nothing signalled yet,
  and a binary-semaphore wait with no queued signal would block the GPU timeline forever). Tracked
  with `m_interopNeedsClientDoneWait`, reset in `ensureInteropColorImage()`.
- Mod (`patches/rtx-remix-minecraft/0006-...patch`): `GlInterop.signalDone()` now actually calls
  `glSignalSemaphoreEXT` on `clientDoneSemaphore` on the binary path instead of being a no-op;
  `beforeRemixPresent()` no longer calls `glFinish()`.

Verified on GPU, fixed view (0.5, 80.0, 0.5, facing south, day, clear), same Ultra preset:
- Before (Phase 1 state, glFinish still present): 58 fps, GPU sm util 71-75%, power ~137-146W
- **After (Phase 2, clientDone wait)**: **92 fps, GPU sm util 99-100%, power ~188-189W**

**+59% fps at the same preset.** The GPU is now the actual bottleneck (99-100% util), matching the
Phase 0 prediction. jstack of the Render thread: 0/5 samples in `glFinish` post-fix vs 3/5
pre-fix — confirmed via 5 fresh samples. No GL errors in either log; 5 screenshots taken while
panning the camera show no tearing or corruption.

Evidence: `evidence/quality-pass/phase2.txt`, `phase2_fixed_screenshot.png`, `phase2_dmon.txt`.

## Phase 3: NRD denoiser — BLOCKED (needs one interactive sudo command)

Researched fully; no code changes needed for the runtime side (it already dlopen()s `libNRD.so`
correctly and version-checks cleanly against this machine's NRD 4.13.0 checkout via a symlinked
header — see `evidence/quality-pass/phase3-nrd-blocked.md`). The only blocker is that building NRD
needs a DXC binary with SPIR-V output, which Arch ships officially as `extra/directx-shader-compiler`
but installing it needs `sudo pacman -S directx-shader-compiler` — an interactive password prompt
this unattended session cannot supply (`sudo: a password is required`).

**To finish Phase 3**: run `sudo pacman -S directx-shader-compiler` once, interactively, then see
the exact CMake/build/wire-up steps in `evidence/quality-pass/phase3-nrd-blocked.md`.

## Phase 4: DLSS/NGX crash investigation — INVESTIGATED, not solved (time-boxed)

Deterministic repro confirmed (identical crash offset in `libnvidia-ngx.so.1` across 3 separate
runs). Ruled out this session with direct evidence:
- Missing required Vulkan extensions — probed `NVSDK_NGX_VULKAN_RequiredExtensions` directly
  against the system driver library; all 5 required extensions (1 instance, 4 device) confirmed
  present in the actual enabled-extensions log from a crash run.
- Wrong/stale NGX library — confirmed the exact driver-matched `libnvidia-ngx.so.615.78.08` loads,
  no `LD_LIBRARY_PATH` pollution from Steam/Flatpak NVIDIA runtimes present on this machine.
- NGX's own internal diagnostic — forced `NVSDK_NGX_LOGGING_LEVEL_VERBOSE` on (investigation-only,
  reverted before commit), reproduced the crash again: zero log lines from NGX before the fault,
  meaning it crashes before NGX's own logging subsystem is live.

Root cause is inside NVIDIA's closed-source driver, unresolvable further without NVIDIA's own
debug symbols or a minimal standalone repro project (recommended next step, see the doc). NGX
stays off by default, matching the already-documented behavior. No patch filed.

Evidence: `evidence/quality-pass/dlss-investigation.md`.

## Summary table

| Phase | Status | Before | After | Evidence | Commit |
|---|---|---|---|---|---|
| 0: Baseline | measured | — | 77fps, GPU 56-70%, glFinish dominant | `baseline.txt` | `7f538c0` |
| 1: Graphics preset | fixed, verified | Turing/Low (misdetected) | Blackwell/Ultra (correct) | `phase1.txt` | `9b851ab` |
| 2: glFinish stall | fixed, verified | 58fps, GPU 71-75% | **92fps, GPU 99-100% (+59%)** | `phase2.txt` | `be2ad8e` |
| 3: NRD denoiser | blocked (sudo) | grainy, no denoiser | unchanged | `phase3-nrd-blocked.md` | `94cfa4f` |
| 4: DLSS/NGX | investigated, not solved | crashes | crashes (root cause isolated to driver) | `dlss-investigation.md` | `530842b` |

## Next step
Phase 3 needs one interactive `sudo pacman -S directx-shader-compiler`, then the build steps in
`evidence/quality-pass/phase3-nrd-blocked.md`. Phase 4 needs either NVIDIA's debug symbols or a
minimal standalone Vulkan+NGX repro project outside Minecraft/dxvk-remix, per
`evidence/quality-pass/dlss-investigation.md`.
