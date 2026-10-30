# Phase 4: DLSS/NGX crash investigation

Time-boxed per the task brief. Goal was to find why the game crashes in
`NVSDK_NGX_VULKAN_Init_Ext2` inside the driver's `libnvidia-ngx.so.1` when NGX is force-enabled
(`REMIX_LINUX_ENABLE_NGX=1`). **Not solved** — root cause is inside NVIDIA's closed-source driver
with no available debug symbols. Findings below rule out every theory this investigation could
test from the application side.

## Reproduction

Deterministic, 100% reproducible on this machine (RTX 5070, driver 615.78.08):

```
REMIX_LINUX_ENABLE_NGX=1 scripts/install.sh --work /mnt/big/rtx-remix-linux --dlss
~/.local/bin/remix-minecraft
```

Crashes every time, within ~10s of process start, during `D3D9InterfaceEx::CreateDeviceEx` (i.e.
on startup, before any world/menu interaction). `coredumpctl info <pid>` gives a full backtrace
(systemd-coredump is configured on this machine). Confirmed over 3 separate runs across this
session — the crash offsets inside `libnvidia-ngx.so.1` are byte-for-byte identical every time
(`+0xc3505` is always the top frame), so this is a deterministic fault given the current inputs,
not a race condition.

## Full backtrace (Render thread)

```
#0-#6  (6 unsymbolized frames inside libnvidia-ngx.so.1, offsets +0xa4fc4 through +0xd5c99)
#7  NVSDK_NGX_VULKAN_Init_Ext2          (libnvidia-ngx.so.1 +0xa98eb)
#8  dxvk::NGXContext::initialize()       (libremix.so)
#9  dxvk::NGXContext::createDLSSContext()
#10 dxvk::DxvkDLSS::DxvkDLSS(DxvkDevice*)
#11 dxvk::DxvkObjects::DxvkObjects(DxvkDevice*)
#12 dxvk::DxvkDevice::DxvkDevice(...)
#13 dxvk::DxvkAdapter::createDevice(...)
#14 dxvk::D3D9InterfaceEx::CreateDeviceEx(...)
#15 remixapi_Startup(...)
#16 Java_com_nvidia_rtxremix_RemixBridge_startup
```

`libnvidia-ngx.so.1` ships with no build-id and no exported symbols beyond the public
`NVSDK_NGX_*` API, so frames #0-#6 cannot be resolved without NVIDIA's own private debug build.

## Theories tested and ruled out this session

1. **Null `vkGetInstanceProcAddr` / `RTLD_LOCAL`** — already fixed in a prior session
   (`patches/dxvk-remix/0011-ngx-pass-Vulkan-loader-entry-points-on-Linux-...patch`, confirmed
   still in place: `rtx_ngx_wrapper.cpp` explicitly passes `getInstanceProcAddr`/
   `getDeviceProcAddr` to `NVSDK_NGX_VULKAN_Init`). The crash is unchanged with this fix present,
   confirming (as the task brief already stated) that this was a real but *different* bug from the
   one still open.

2. **Missing required Vulkan extensions** — tested directly, ruled out. Wrote a small probe
   (`dlopen("libnvidia-ngx.so.1")` + call `NVSDK_NGX_VULKAN_RequiredExtensions`) against the
   actual system driver library:
   ```
   Instance extensions (1): VK_KHR_get_physical_device_properties2
   Device extensions (4):   VK_NVX_binary_import, VK_NVX_image_view_handle,
                             VK_KHR_buffer_device_address, VK_KHR_push_descriptor
   ```
   Cross-checked against `remix-dxvk.log`'s "Enabled instance/device extensions" dump from an
   actual crash run: **all 5 are present**. `VK_KHR_get_physical_device_properties2` is
   `insExtensions.khrDeviceProperties2` in `dxvk_extensions.h`, mode `Required`. The 4 device
   extensions all appear in the "Enabled device extensions:" log block. This rules out an
   extension mismatch as the cause.

3. **Wrong/stale NGX library loading** — ruled out. `/usr/lib/libnvidia-ngx.so.1` resolves
   (`readlink -f`) to `/usr/lib/libnvidia-ngx.so.615.78.08`, exactly matching the installed
   `nvidia-utils` package version (615.78.08-1) and the driver reported by `nvidia-smi`/F3. No
   `LD_LIBRARY_PATH` or `LD_PRELOAD` pollution from Steam/Flatpak NVIDIA runtimes (those exist on
   this machine for other games but are not in the launch script's environment, confirmed via
   `/proc/<pid>/environ`).

4. **NGX's own internal diagnostic** — attempted, inconclusive. Temporarily forced
   `featureCommonInfo.LoggingInfo` on unconditionally (was `#ifndef NDEBUG`-gated) at
   `NVSDK_NGX_LOGGING_LEVEL_VERBOSE`, rebuilt, reproduced the crash again (same offsets). **Zero**
   `"DLSS Message:"` lines appeared in either log before the crash. This itself is informative:
   NGX's own app-logging callback never fires, meaning the fault happens very early inside
   `NVSDK_NGX_VULKAN_Init_Ext2` — before NGX's internal logging subsystem is live, or on a code
   path that bypasses it entirely (e.g. a hardware/feature capability probe). This debug change
   was reverted before committing (`git checkout -- rtx_ngx_wrapper.cpp` in the dxvk-remix tree);
   it is not shipped as a patch.

## Not reached (out of time-box)

- (c) Feature discovery params / exact Vulkan handles passed to `Init_Ext2` — the call site itself
  (`rtx_ngx_wrapper.cpp:244`) looks correct by inspection (valid `vkInstance`/`vkPhysicalDevice`/
  `vkDevice` handles from the already-successfully-created `DxvkDevice`, non-null proc addrs,
  `NVSDK_NGX_FeatureCommonInfo` with a valid `PathListInfo` pointing at `remix-sdk/`), but was not
  byte-level verified against what a known-working Windows NGX init passes.
- (d) Fine-grained driver/NGX SDK compatibility matrix (615.78.08 driver vs. DLSS SDK snapshot
  310.9.1) — would need NVIDIA's own compatibility documentation, not publicly available in enough
  detail to confirm or rule out.
- (e) App data path write permissions — not the cause (path exists, is writable, the game already
  writes crash reports and logs there successfully), but not exhaustively tested under NGX
  specifically since the crash happens before any file I/O from NGX would occur.

## Recommended next steps

1. This is almost certainly a bug or an undocumented requirement in NVIDIA's closed-source
   `libnvidia-ngx.so.1` on Linux, not something fixable from dxvk-remix's call site without more
   information. The deterministic, identical-offset crash across multiple runs rules out
   environmental flakiness.
2. Best path forward: build a minimal, ~50-line standalone Vulkan+NGX reproduction (create a bare
   Vulkan 1.4 instance/device with exactly the extensions above, call `NVSDK_NGX_VULKAN_Init_Ext2`
   with minimal `FeatureCommonInfo`) outside Minecraft/dxvk-remix entirely. If it still crashes,
   that is a clean, shareable repro for an NVIDIA bug report; if it does *not* crash, the
   difference between that minimal repro and dxvk-remix's actual call (additional enabled
   features/extensions, instance layers, device creation flags) narrows the real cause fast.
3. Given this requires either NVIDIA's own debug symbols or a non-trivial new reproduction
   project, it was not attempted in this already-long session — flagging as the clear next step
   rather than guessing further.

NGX stays off by default on Linux (`REMIX_LINUX_ENABLE_NGX` unset), matching the documented
behavior from `patches/dxvk-remix/0012`. No new patch filed for Phase 4.
