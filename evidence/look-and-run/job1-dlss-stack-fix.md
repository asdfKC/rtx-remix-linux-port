# Job 1: DLSS/NGX crash — root cause found, real fix applied, new blocker found

## Root cause: JVM thread stack too small for NGX init

The repro (`tests/ngx_init_min`) never crashed in the previous session because it ran on a
process's **main thread**, which gets the full `ulimit -s` stack (8MB by default on this
machine). The game's crash happens on the JVM's **"Render thread"**, a secondary Java thread
that native/JDK code gives a much smaller default stack on Linux.

Confirmed directly by shrinking the repro's own stack:

| `ulimit -s` | Result |
|---|---|
| 8192 KB (default) | `done, no crash` |
| 4096 KB | `done, no crash` |
| 2048 KB | `done, no crash` |
| **1024 KB** | **Segfault**, `coredumpctl` backtrace: `libnvidia-ngx.so.1 + 0xc3505` inside `NVSDK_NGX_VULKAN_Init_Ext2` — **identical offset** to the real game crash |
| 512 KB | Segfault, same offset |

The threshold is between 1MB and 2MB. This is exactly consistent with the JVM's real crash
(`+0xc3505` inside `NVSDK_NGX_VULKAN_Init_Ext2`, same call, reached through
`dxvk::NGXContext::initialize() -> createDLSSContext() -> DxvkDLSS::DxvkDLSS()` from the real
coredump of pid 2066776).

## A second bug hid the fix: Prism's `AutomaticJava=true`

Applying `-Xss8m` via the instance's `JvmArgs` had **no effect** the first time it was tried —
the game still crashed at the identical offset. Checking `~/.local/share/PrismLauncher/logs/`
for the exact command line Prism actually executed showed **none** of the instance's custom
`JvmArgs` were passed at all (no `-Xss8m`, but also missing the pre-existing G1GC flags that
had been configured for this instance all along). The instance's `instance.cfg` had
`AutomaticJava=true` in `[General]`, which makes Prism silently re-detect its own Java runtime
and flags at launch time, ignoring `JavaPath`/`JvmArgs`/`OverrideJavaArgs` entirely. This had
apparently been silently broken since the instance was first created — no earlier phase's FPS
or preset measurements depended on the custom `JvmArgs`, so it went unnoticed.

Fix: `instance.cfg` `[General]` section now has `AutomaticJava=false`, with `JavaPath`,
`JavaVersion`, `JvmArgs` (including `-Xss8m`), `MinMemAlloc`/`MaxMemAlloc`, `OverrideJavaArgs`,
`OverrideJavaLocation`, `OverrideMemory` all moved into `[General]` alongside it (Prism's own
instance settings dialog had previously split them across `[General]`/`[UI]` in a way that,
combined with `AutomaticJava=true`, meant none of them took effect).

`scripts/install.sh` is patched to add `-Xss8m` to the JVM args it writes and to always set
`AutomaticJava=false`, so a fresh install gets this fix automatically.

## Verified: the NGX crash is gone

With the fix applied and NGX/DLSS libraries staged + `REMIX_LINUX_ENABLE_NGX=1`:
- The process survives past `NGX init:` logging (previously crashed within ~1 second of that
  line, every time, for 3+ sessions).
- Reaches the main menu, screenshot: `job1_dlss_fixed_menu.png`.
- Loads the "New World" singleplayer world fully (entity materials, render passes created) with
  no coredump.
- `coredumpctl list` shows no new crash for this run (the only NGX-offset crashes remaining in
  the list are from before the fix, timestamped 07:40-07:41).

## New blocker found: render thread hangs once gameplay actually starts with DLSS

After the world finishes loading with DLSS/NGX enabled, the "Render thread" stops presenting
frames entirely:
- `nvidia-smi` GPU utilization: **0%**, sustained (not a transient stall).
- `jstack` on the Java process shows "Render thread" `RUNNABLE`, stuck inside the native call
  `RemixBridge.present` (`RemixAPI.present -> RemixRenderer.presentFrameVk -> native present`),
  consuming only ~6% CPU over a 5-minute sample (not spinning hot, not blocked in `WAITING`
  either — consistent with a periodic-retry loop that never succeeds).
- `remix-dxvk.log` stops cleanly after the one-time render-pass setup lines
  (`RenderPass GBuffer Raytrace Mode`, `Integrate Indirect Mode: Spatially Hashed Radiance
  Cache - activated`) — no per-frame log lines follow, and none resume even after minutes.
- This does **not** reproduce with NGX/DLSS off: the same instance, same JVM args fix, same
  world, same fixed view renders normally (GPU 100%, F3 showed 37fps at a different in-world
  spot while sanity-checking, screenshot `job1_dlss_off_fixed_view.png` confirms the fixed view
  itself renders fine with DLSS off).

Likely candidate (not confirmed): the Phase 2 `clientDone` semaphore wait added in
`patches/dxvk-remix/0014` (`D3D9SwapchainExternal::Present()` waits on
`m_interopClientDoneSemaphore` before each blit, skipped only on the first blit of a new
image) may never get signalled once DLSS is active, if DLSS's internal resolution scaling
changes the interop image's resize/recreation path in a way the mod's
`GlInterop.signalDone()` doesn't cover. Not investigated further this session — out of the
~30-45 minute budget for this job once the stack-size root cause and the `AutomaticJava` bug
were already found and fixed.

## Decision

- Keep the `-Xss8m` + `AutomaticJava=false` fix — it fixes a real, previously-undiagnosed crash
  and a real, previously-unnoticed "JvmArgs never applied" bug, with no observed downside
  (confirmed non-DLSS gameplay still renders correctly, GPU-bound, same as before).
- **NGX/DLSS stays off by default** (`REMIX_LINUX_ENABLE_NGX` unset in the generated launch
  script) — the crash is fixed but the subsequent render hang means DLSS is not yet usable
  end-to-end. Documented as the clear next blocker for a future session: instrument
  `GlInterop.signalDone()` / `m_interopNeedsClientDoneWait` specifically on the DLSS-enabled
  path, or temporarily revert patch 0014's wait to confirm/rule it out.

## Files

- `evidence/look-and-run/job1_dlss_fixed_menu.png` — main menu rendering with DLSS/NGX
  enabled, past the point that always crashed before.
- `evidence/look-and-run/job1_dlss_off_fixed_view.png` — fixed test view, DLSS off, confirming
  the JVM args fix doesn't regress normal rendering.
- `scripts/install.sh` — `-Xss8m` added to `JVM_ARGS`, `AutomaticJava=false` added.
- `~/.local/share/PrismLauncher/instances/rtx-remix-linux/instance.cfg` — fixed locally to
  match (backed up to `instance.cfg.bak` before editing).
