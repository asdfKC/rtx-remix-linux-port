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

## Phase 1: wrong graphics preset — NOT STARTED

## Phase 2: per-frame stall (glFinish) — NOT STARTED
Baseline evidence above already points at hypothesis (a). Next: replace `glFinish` with a real
semaphore wait or ring-buffered interop image.

## Phase 3: NRD denoiser — NOT STARTED

## Phase 4: DLSS investigation — NOT STARTED

## Next step
Phase 1 (graphics preset / Blackwell detection) — cheap, do it next, then move into Phase 2
since that's the highest-value fix per the baseline evidence.
