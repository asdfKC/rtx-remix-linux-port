# Job B: DLSS/NGX crash — minimal repro does NOT reproduce it

## Summary

Built the minimal standalone NGX init repro the Phase 4 investigation recommended
(`tests/ngx_init_min.c`). Every variant succeeds cleanly on this machine (RTX 5070, driver
615.78.08) — including one that matches the real crash run's Vulkan instance/device
configuration in detail (same device extensions, ray tracing + buffer-device-address +
synchronization2 features, same 3-queue layout, same Vulkan API version, same
`NVSDK_NGX_FeatureCommonInfo` fields dxvk-remix passes, same `RTLD_LOCAL` dlopen hosting
pattern the JNI bridge uses). This rules out a plain "any valid `NVSDK_NGX_VULKAN_Init_Ext2`
call crashes on this driver" bug, and narrows the cause to something specific to being invoked
from inside dxvk-remix's actual process (JVM + JNI bridge + the full DXVK library graph) that
this repro does not replicate. **Not solved** — out of this session's time-box before finding
the actual differentiator.

## What was tested (all succeed, "done, no crash", `NVSDK_NGX_Result_Success`)

Build: `tests/build_ngx_init_min.sh` (links `libnvsdk_ngx.a` from
`dxvk-remix/submodules/dlss/lib/Linux_x86_64`, same static NGX loader dxvk-remix links).

1. **Variant a** — minimal `FeatureCommonInfo` (all zero), no `PathListInfo`, 4 extensions
   (the ones `NVSDK_NGX_VULKAN_RequiredExtensions` reports), 1 queue. Succeeds.
2. **Variant b** — same as (a) plus `PathListInfo` pointing at the directory containing the
   DLSS feature `.so` files. Succeeds.
3. **Variant c (minimal device)** — same as (b) plus verbose NGX logging
   (`NVSDK_NGX_LOGGING_LEVEL_VERBOSE`, matching dxvk-remix's debug-build logging path). NGX's
   own log shows it finding and loading `libnvidia-ngx-dlss.so`, `-dlssg.so`, `-dlssd.so` from
   the path, fully initializing all three, and returning `Success`. Succeeds.
4. **Variant c (full device)** — same as (3), but the Vulkan device now matches the actual
   crash run's `remix-dxvk.log` "Enabled device extensions:" block exactly (41 extensions,
   including `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`,
   `VK_KHR_ray_query`, `VK_KHR_synchronization2`, `VK_KHR_buffer_device_address`, with their
   feature structs chained and enabled), 3 queue families (graphics/transfer/async compute,
   matching the log's "Queue families:" block), and Vulkan 1.4 API version (matching the log's
   "Vulkan: 1.4.351"). Still succeeds.
5. **Variant c, `dlopen(RTLD_LOCAL)`-hosted** — same device config as (4), but built as a
   shared library and loaded into a tiny host executable with `dlopen(..., RTLD_NOW |
   RTLD_LOCAL)` instead of linking statically into the test binary — the same loading flag the
   JNI bridge uses for `libremix.so` (and the subject of the once-fixed
   `patches/dxvk-remix/0011` bug about `vkGetInstanceProcAddr` visibility). Still succeeds.

None of these reproduce the deterministic crash dxvk-remix hits at `libnvidia-ngx.so.1
+0xc3505` inside `NVSDK_NGX_VULKAN_Init_Ext2`, confirmed still present this session (crashed
the game process within ~10s of `REMIX_LINUX_ENABLE_NGX=1 scripts/install.sh --dlss`, same as
documented in `evidence/quality-pass/dlss-investigation.md`).

## What this rules out vs. what it narrows down

Ruled out as the sole cause (all tested directly, all matched):
- Missing/extra Vulkan device extensions
- Missing ray tracing / buffer-device-address / synchronization2 feature flags
- Queue family layout/count
- Vulkan API version mismatch
- `NVSDK_NGX_FeatureCommonInfo` contents (`PathListInfo`, logging)
- `RTLD_LOCAL` dlopen hosting by itself

Narrowed to something present only in the real process that none of the above replicate:
- Being invoked from the actual JVM (Java 21 OpenJDK) + JNI bridge, not a small C host
- Being invoked from inside dxvk-remix's full ~327 MB `libremix.so` (all of DXVK, USD, slang
  runtime, etc. loaded alongside), not a ~30 KB test binary — possible symbol/TLS/heap
  interaction this repro's tiny symbol table can't trigger
- The Vulkan loader entry points dxvk-remix actually passes: `rtx_ngx_wrapper.cpp` passes
  `&vkGetInstanceProcAddr` (the raw libvulkan symbol, same as this repro) and a
  `vkGetDeviceProcAddr` looked up through it — believed identical to what this repro does, but
  not byte-verified against DXVK's own loader wrapper layer
- Thread context: dxvk-remix calls this from its "Render thread" (one of several JVM-spawned
  native threads), not a process's sole thread
- The Steam Proton Vulkan layers visible in the real run's "Vulkan instance layers:" log
  (`VK_LAYER_VALVE_steam_overlay_32/64`, `VK_LAYER_VALVE_steam_fossilize_32/64`,
  `VK_LAYER_MESA_anti_lag`, `VK_LAYER_MESA_device_select`) — present in the system's global
  layer registry and potentially auto-enabled by the loader even though dxvk-remix doesn't
  explicitly request them, not disabled in this repro

## Recommended next steps (not attempted — out of this session's budget)

1. Highest-value remaining test: run with `VK_LOADER_LAYERS_DISABLE` or
   `VK_INSTANCE_LAYERS=` cleared to rule the Steam/MESA layers in or out — they are the one
   difference in the "Vulkan instance layers" log block this repro doesn't control (they come
   from the loader's global layer discovery, not explicit enablement, so can't simply be
   "added" to the repro — they'd need to be suppressed in the real run instead to test their
   absence).
2. If that doesn't explain it, attach to the real game process at the crash point with `gdb`
   (coredumpctl has the memory image already: `coredumpctl gdb` on the most recent NGX crash)
   and compare the actual arguments/register state at the `Init_Ext2` call against this
   repro's — a byte-level diff of what's on the stack, not just what the source code says is
   passed.
3. Given the clean minimal repro, this is now a much stronger candidate for an NVIDIA bug
   report than Phase 4 could show: the SDK works in multiple configurations in isolation, and
   fails identically every time inside one specific real application. That pattern (not a
   simple "any call crashes") would need the coredump/backtrace from step 2 to be a complete,
   actionable report — not filed this session.

NGX stays off by default on Linux (`REMIX_LINUX_ENABLE_NGX` unset). No runtime code changed.

## Files

- `tests/ngx_init_min.c`, `tests/build_ngx_init_min.sh` — the repro, kept in the repo for the
  next session to extend (e.g. with the layer-suppression test above).
