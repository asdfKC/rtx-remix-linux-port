# RTX Remix for Minecraft: native Linux port, porting notes

## Phase 0: feasibility triage (2026-10-25)

Sources: rtx-remix-minecraft @ f2c6fdaa (2026-10-23), dxvk-remix main @ bbd74a12 (2026-10-23),
also checked tag remix-9.4.0 (2026-10-16).

### Verdict: NO-GO for Phase 2 as scoped (blocked on unpublished runtime source)

**The blocker is the runtime's API version, not the platform.**

- The mod's copied header `rtxremix-java/native/include/remix_c.h` is Remix API **0.16.0**.
- Public dxvk-remix `main` is API **0.6.6**. Tag `remix-9.4.0` is **0.6.5**.
- The mod pins a private runtime build, `rtx_remix_runtime@minecraft-rtx2-71a581da-win64-release`
  (Packman binary, Windows only). Its own docs say so, in `docs/GETTING_STARTED.md:111`:
  "The dxvk-remix source of the pinned runtime is not public yet, so a matching runtime
  cannot be built from public source."
- The runtime requires an exact minor-version match while major is 0, so a 0.6 runtime refuses a
  0.16 client (`INCOMPATIBLE_VERSION`, error 8).

The mod uses these entry points, and the public runtime has none of them:

```
dxvk_GetInteropInfo            <- the whole GL<->Vulkan interop path
CreateTextureFromMemory / UpdateTextureFromMemory / CreateTextureFromFile / DestroyTexture
SceneInstanceAdd / SceneInstanceUpdate / SceneInstanceRemove   <- retained instances (terrain, entities)
Submit
UpdateMaterial
AcquireConfigLayer / ReleaseConfigLayer / SetConfigLayerBlending / GetConfigVariableResolved
```

A grep of public `src/` and `public/` for `InteropInfo`, `interopImage`, `skipVulkanSwapchain` and
`TEXTURE_FROM_MEMORY` finds nothing. The interop image, its exported semaphores and memory, and
the client-wait-before-present flow do not exist in public source at all.

Porting public dxvk-remix to Linux would give a runtime the mod cannot use. To make it usable we
would also have to **re-implement ~10 API minor versions of renderer features** against the private
ABI: retained scene instances, texture upload and patching, config layers, interop, and Submit.
That work is a fork of NVIDIA's unreleased work. It is far beyond the budget, and it would almost
certainly break against the real 0.16 semantics, like struct layouts and `rtx_remix_specialization.inl`.

### Secondary findings (would matter once 0.16 source is public)

| Area | Finding | Linux outlook |
|---|---|---|
| Build entry | `meson setup` fails at once: `meson.build:46` runs `scripts-common/update-deps.cmd` (cmd/Packman). | Patchable: skip Packman, use system tools. |
| Packman deps | All pinned packages are Windows builds: OpenUSD 25.11 win64, slang win64, glslang, spirv-tools, RTX IO, NRD, NGX DL-FG/DL-NR, Reflex, Aftermath, Sentry, Nsight SDK. | glslang/spirv-tools/vulkan from Arch `extra`. slang: needs a source build. USD: needs a source build (large) or must be dropped. RTX IO: `-Denable_rtxio=false`. Reflex/Aftermath/Nsight/Sentry: stub or disable. NRD: open source, rebuildable. |
| Link deps | `find_library` for vulkan-1, d3d9, d3d11, dxgi, shlwapi, delayimp, d3dcompiler, dbghelp. | Need a Linux branch in meson.build. |
| Win32 code | 36 files under `src/` include windows.h. 21 in dxvk/util use CreateFile/LoadLibrary/HMODULE/etc. (e.g. `rtx_asset_data_manager.cpp` uses CreateFileMapping). | Upstream DXVK is portable, so most of this is Remix-added. Moderate effort. |
| Win32 external handles | `dxvk_image.cpp:126,266` (import/export memory) and `rtx_semaphore.cpp:86-92` (export semaphore) use `*Win32HandleKHR`. | Straightforward to add `*FdKHR` variants. |
| DLSS | The `submodules/dlss` SDK (310.9.1) **does ship Linux x86_64 libs**: `libnvidia-ngx-dlss.so` (SR), `libnvidia-ngx-dlssd.so` (RR), and `libnvidia-ngx-dlssg.so` (FG). | SR/RR look feasible. FG is present in the SDK but Remix pulls its own `ngx_sdk_dlfg` Packman pkg. Linux FG viability is unverified. |
| Bridge, Detours | Only used by `bridge/` (32-bit games). | Not needed. |

Not evaluated, because the blocker made it moot: RTXDI/NRC/SHARC/RTXCR/XeSS Linux builds,
NVAPI usage, and whether the C API can be exported from a plain .so without the D3D9 layer.

### What is still independently doable

- **Phase 1 (JNI bridge → POSIX, CMake)** and **Phase 3 (Java: lib names, GlInterop FD path,
  `gradlew`, `install.sh`)** do not depend on the runtime source. They could be written,
  compiled and unit-tested now, ready for when the 0.16 runtime source or a Linux runtime appears.
  They cannot be exercised end to end until then.
- The runtime's FD export would need a small header-level ABI decision. `InteropInfo` fields are
  `uint64_t *_Handle`, which can carry an fd, but ownership/dup semantics need agreement with NVIDIA.

### Spend so far
Roughly $5 of the ~$100 budget (cloning, header diff, one meson configure).

---

## Phase A: ABI diff, mod API 0.16.0 vs public dxvk-remix 0.6.6 (2026-10-25)

Method: `diff -u` of public `public/include/remix/remix_c.h` against the mod's
`rtxremix-java/native/include/remix_c.h`, cross-checked against every `g_remix.*` call and every
`remixapi_*` type in `remixbridge.cpp`. The full diff is 677 lines.

### Gate result: PASS
No used struct changed in a way the 0.6 renderer cannot represent. Every change is one of:
appended fields, `remixapi_Path` replaced by `remixapi_TextureHandle` (both pointer-sized), or
removed trailing fields. The cleanest approach is for the fork to adopt the mod's 0.16 header
verbatim and to adapt `rtx_remix_api.cpp` to it.

### Functions the bridge calls

| Function | Status | Notes |
|---|---|---|
| Startup, Shutdown, Present | field-added | StartupInfo `+interopImage`. PresentInfo `+skipVulkanSwapchain, interopFlipY, clientDoneSemaphore_ValueToWait, disableCpuWaitForExternalSemaphores`. |
| CreateMaterial, DestroyMaterial | changed (texture refs) | See the struct table. |
| CreateMesh, DestroyMesh | field-added | See MeshInfoSurfaceTriangles. |
| DrawInstance, CreateLight, DestroyLight, DrawLightInstance, SetupCamera, SetCameraMediumMaterial, SetConfigVariable | identical signature | SetConfigVariable doc now routes NoSave keys (`rtx.showUI`) to the Derived layer, which the mod's input gate relies on. |
| dxvk_CreateD3D9, dxvk_RegisterD3D9Device, dxvk_GetExternalSwapchain, dxvk_GetVkImage, dxvk_CopyRenderingOutput, dxvk_SetDefaultOutput | identical | The JNI wraps all of them. RemixRenderer uses only the Startup path. |
| UpdateMaterial | **missing** | `(handle, const MaterialInfo*)` |
| CreateTextureFromMemory, CreateTextureFromFile, DestroyTexture, UpdateTextureFromMemory | **missing** | New TextureFormat (9 values) and TexturePurpose (6 values). Batched sub-rect patches with mip regen. |
| SceneInstanceAdd, SceneInstanceUpdate, SceneInstanceRemove | **missing** | Take a plain `remixapi_InstanceInfo`, which is unchanged. |
| Submit | **missing** | `SubmitInfo{interopFlags}`. It allocates the interop image and renders. |
| AcquireConfigLayer, SetConfigLayerBlending, ReleaseConfigLayer, GetConfigVariableResolved | **missing** | Layer priority/strength/threshold blending semantics are specified in comments only. `documentation/RemixConfig.md` in public main needs checking for overlap. |
| dxvk_GetInteropInfo | **missing** | New `remixapi_dxvk_InteropInfo`, covering color plus primary depth, timeline semaphores. |

### Structs used

| Struct | Status | Impact |
|---|---|---|
| MaterialInfo, MaterialInfoOpaqueEXT, MaterialInfoOpaqueSubsurfaceEXT, MaterialInfoTranslucentEXT | Path→TextureHandle (same size). Opaque and Translucent **lose** the six trailing texture-transform fields. | The runtime must map handle→texture instead of path→asset. Public code reads the transform fields when `apiVersion >= 0.6.6`. Reporting 0.16 would make it read past the struct end, so that gate must be rewritten. |
| LightInfoDomeEXT | Path→TextureHandle, `+opacity, +blendType` | `opacity` and `blendType` are new semantics, initially ignored. |
| HardcodedVertex | same 64-byte size. `texcoordBounds[4]` (half floats) replaces two pads. | Atlas UV-bounds wrapping is flag-gated. Initially ignored (KNOWN_GAP). |
| MeshInfoSurfaceTriangles | `+flags, +reserved` (appended). | **Array stride changes**, so the runtime must be compiled against the 0.16 header. |
| StartupInfo, PresentInfo | appended | see above |
| InstanceInfo (+Blend, BoneTransforms, GpuInstancing EXT), CameraInfo (+Parameterized), CameraMediumInfo, LightInfo (+Sphere/Rect/Disk/Cylinder/Distant EXT), MeshInfo, Transform, Float4D | identical | none |
| TextureFromMemoryInfo, TexturePatch(Info), TextureBatchEntry, SubmitInfo, ConfigVariableResolvedInfo, dxvk_InteropInfo | new | implement |
| StructType enum | appended 28–31 | register in `rtx_remix_specialization.inl` |
| ErrorCode | appended 12 INDEX_OUT_OF_RANGE, 13 BUFFER_TOO_SMALL | none |

### Present/window model (important for Phase B)
`RemixRenderer.presentFrameVk()` uses `skipVulkanSwapchain = 0` and `interopFlipY = 1`. GL blits
its final frame back into the interop image, and **Remix presents through its own Vulkan swapchain
on Minecraft's HWND** (`glfwGetWin32Window`), replacing `glfwSwapBuffers`.

Public dxvk-remix is forked from **pre-native DXVK**. It has no `src/wsi`, no
`include/native` Windows-type shims, and its presenter (`src/vulkan/vulkan_presenter.cpp`) is
Win32-surface only. Proposal: on Linux, make the runtime **headless**. It would never create a
VkSurface, the mod would set `skipVulkanSwapchain = 1`, and GL would keep `glfwSwapBuffers`.
That removes WSI porting from the runtime entirely, and it keeps the swapchain on the GL/PRIME
path, which already works on his desktop. The cost is a small mod-side change in `presentFrameVk`.

---

## Phase B probe: dependency inventory for a native Linux runtime

| Dep | How it's used | Linux source | Proposed |
|---|---|---|---|
| OpenUSD 25.11 | **Hard link dep**: 21 libs (usd_*, tbb12), asset-replacement/mod system, `src/usd-plugins`. | Packman pin is win64 only. Not in the allowed source list. | (a) build OpenUSD from source (large, needs TBB), or (b) compile out USD and stub the replacement/capture subsystem. The mod drives everything via the API and ships `.conf` + textures, not USD mods. **Decision needed.** |
| slang (shader compiler) | All RTX shaders, build time + runtime recompile. | win64 Packman pin. Not allowed as-is. | Build shader-slang from source at the pinned tag (`2025.10.4`, which has a remix-specific suffix `-g496fab196-remix`, so it may carry NVIDIA patches), or compile shaders on Windows and ship SPIR-V. **Decision needed.** |
| glslang, spirv-tools, Vulkan headers | build-time tools | Arch official packages (`glslang`, `spirv-tools`, `vulkan-headers`). Couldn't confirm versions, because archlinux.org is blocked from this container. | use distro |
| NRD 4.13 (denoiser) | linked, Packman win64 | not in allowed list (NRD source is public on GitHub) | build NRD from source, or remove and rely on DLSS-RR only. **Decision needed.** |
| NRC (`submodules/nrc`) | neural radiance cache | **Windows-only** binaries (NRC_Vulkan.dll + CUDA 13 dlls) | disable (KNOWN_GAP) |
| XeSS (`submodules/xess`) | Intel upscaler | Windows-only libs | disable |
| NVAPI (`submodules/nvapi`) | NVIDIA Windows driver API | Windows-only | stub |
| DLSS SR/RR (`submodules/dlss`) | NGX | **Linux x86_64 .so present** (310.9.1) | wire up |
| DLSS-FG, Reflex, Aftermath, Nsight SDK, Sentry, RTX IO | optional | Windows Packman pins | disable/stub (FG and Reflex lost) |
| d3d9/COM types on Linux | the whole `src/d3d9` + `src/dxvk` uses Windows types (pre-native DXVK) | none in repo | Needs Windows-type shim headers, as upstream DXVK's `include/native/` provides (zlib-licensed, public). Not in the allowed source list. Alternatively, Arch `mingw-w64-headers` for `d3d9.h` plus a hand-written shim. **Decision needed.** |

---

## Phase B: running log (newest last)

Decisions approved by the user: USD compiled out; slang built from source at the pinned tag; NRD from
public source; DXVK `include/native` headers; headless runtime with `skipVulkanSwapchain = 1`.

1. **Meson configures on Linux.** A self-contained Linux block sits at the top of the root
   `meson.build` and ends in `subdir_done()`, so the Windows path is untouched. It skips Packman,
   takes glslang/spirv-val from the distro and slangc from `external/slang`, links DLSS from
   `submodules/dlss/lib/Linux_x86_64`, and turns off USD, RTX IO, Sentry, Reflex, Aftermath and Nsight.
2. **Compat layer.** `linux/include/remix_linux_compat.h` is force-included in every C++ TU. It
   provides POSIX-backed Win32 calls (dlopen/dlsym, UTF-8 conversion, VK_* key codes, bit-scan
   intrinsics). Win32-only util files are replaced by `linux/src/util_linux.cpp`, which is headless:
   window and monitor queries report a client size the API sets. `util_env.cpp` has a real POSIX branch.
3. **Compiles:** `src/util`, `src/spirv`, `src/vulkan`, `src/tracy`.
4. **Shaders, finding 1.** Public slang v2025.10.4 rejects `-zero-initialize`. Upstream #6058 removed
   the parse case but kept the semantics. Local fix: re-add the `case`.
5. **Shaders, finding 2.** Public v2025.10.4 emitted invalid SPIR-V (`OpUConvert` vector→scalar) in
   242 variants: gbuffer, integrate_*, volume ReSTIR, RTXDI gradients. The cause is 16-bit unsigned
   vector → 32-bit signed vector conversions. Fixed upstream 13 days after the tag, in #7398, with the
   related #7448. Both are cherry-picked; that these are among NVIDIA's 39 private commits is a guess,
   not proven.
   **Flag:** any further shader failure may be another NVIDIA slang patch.
6. Case-sensitivity fixes: NRC's `Include/`, `Nrc.hlsli` and `NrcHelpers.hlsli`.
   Packman data (fonts) can't be fetched from the build container, so it uses DejaVu placeholders
   *in the container only*. The real build fetches `rtx-remix-fonts@2` with the repo's Linux Packman script.
7. **All RTX shaders compile and pass spirv-val (584 headers)** with the patched public slang.
   This is compile-level only: SPIR-V validity says nothing about rendering correctness
   compared with NVIDIA's slang build.
8. USD compiled out via `REMIX_NO_USD` (material/light USD deserializers, GameCapturer stub).
   DLSS-NR compiled out via `REMIX_NO_DLSSNR`, because its headers are only in the Windows NGX package.
   GCC conformance fixes for code MSVC accepted. `src/dxvk/dxvk_device.cpp` passes a syntax check.
   A full syntax sweep of `src/dxvk` is next: 74 of 173 files failing at last count.
9. **`src/dxvk`: 167 of 169 files pass a syntax check** (GCC 13, `-fsyntax-only` with the PCH).
   New Linux pieces:
   - FD-based interop export and import (`VK_KHR_external_memory_fd` / `_semaphore_fd`).
   - Headless ImGui platform backend.
   - NRC/NVAPI stubs that return "unsupported".
   - No-op Reflex and file watch.
   - DDS loading through `mmap`.
   - `libNRD.so` loaded with `dlopen`.

   Two real bugs found along the way:
   - Ray tracing extensions were gated on `_WIN64`, so a Linux build would have had no
     `VK_KHR_acceleration_structure` or `VK_KHR_ray_query`.
   - Without RTX IO, the async texture loader was never started.
10. Interface notes for Phase C:
    - Remix's ImGui menus get input from Win32 messages. On Linux the mod must forward input.
    - `Startup` sizes the device from `GetClientRect(hwnd)`. On Linux that is a client size the
      mod reports (`remix_linux::setHeadlessClientSize`).

---

## Phase B gate: PASSED (compiles and links)

**`libremix.so` compiles and links natively on Linux x86_64** (Ubuntu 24.04 build container, GCC 13.3,
meson 1.12, slang v2025.10.4 plus patches, glslang 15.1, SPIRV-Tools 2025.1).

| Check | Result |
|---|---|
| All 584 RTX shader variants compile and pass `spirv-val` | yes |
| All C++ in util/spirv/vulkan/dxvk/dxso/d3d9 plus the `linux/` sources compiles | yes |
| `libremix.so` links (`-Wl,--no-undefined`) | yes. 318 MB, mostly embedded SPIR-V. Not stripped, no `-g` |
| `NEEDED` | only `libvulkan.so.1`, `libm`, `libc`, `ld-linux`. libstdc++/libgcc are static |
| Exported symbols | exactly `remixapi_InitializeLibrary` and 3 `remixinternal_*` (version script) |
| `tests/load_remix.c`: dlopen + `remixapi_InitializeLibrary(0.6.6)` | SUCCESS, 22/22 interface entries non-null |
| `tests/startup_remix.c` with no Vulkan driver | `Startup` returns `0x88960003` (instance extension failure) cleanly |
| `tests/startup_remix.c` on Mesa lavapipe (software Vulkan with KHR ray tracing) | gets through instance, adapter and device creation, NGX init (reports unsupported), and NRD load (missing, disabled). Then fails, as designed, at presenter surface creation (`evidence/phaseB-startup-lavapipe.log`) |
| **Verified on GPU** | **no**. Nothing has run on an NVIDIA GPU |

### Dependencies: built, stubbed, dropped
| Component | Linux status |
|---|---|
| slang | Built from source, v2025.10.4 + upstream #7448 and #7398 cherry-picks + a `-zero-initialize` parse fix. Remix's pin is NVIDIA-internal (`-39-g496fab196-remix`). The cherry-picks fixed the only shader failures seen. Other NVIDIA patches may exist and may affect rendering. |
| NRD 4.13.0 | Headers and shader includes used. **`libNRD.so` is not built yet.** The runtime disables NRD when it is missing. Building it needs MathLib, ShaderMake and DXC: **decision needed**. |
| DLSS SR / RR | Linked against `libnvsdk_ngx.a` (Linux). The runtime libraries are `libnvidia-ngx-dlss.so` / `-dlssd.so` from the submodule. NGX init ran on lavapipe and reported unsupported. **Untested on NVIDIA.** |
| DLSS-FG (DLFG), DLSS-NR | FG is left in (frame generation is out of scope). NR is compiled out because its headers are missing. |
| USD, game capture, USD mods | compiled out (approved) |
| NRC, XeSS, NVAPI | stubbed as "unsupported", using the SDK headers' own declarations |
| Reflex, Aftermath, Sentry, RTX IO, Nsight | compiled out or no-op |
| OpenVR/OpenXR | inert (registry stubs fail, so VR stays off) |
| Window, cursor, monitor, GDI, file watch | headless replacements |

### Bugs found and fixed that compile and link alone would not show
- Ray-tracing device extensions were gated on `_WIN64`.
- The platform provider requested `VK_KHR_win32_surface`, which would fail `vkCreateInstance` on Linux.
- Without RTX IO, the async texture loader thread never started.
- `remix_c.h` was not usable from C on non-Windows (`wchar_t`).

### Known open items carried into Phase C
- **Presenter:** `Startup` still creates a D3D9 swapchain and presenter. The headless path must skip
  surface creation entirely (part of the interop/`skipVulkanSwapchain` work).
- **Client size:** `GetClientRect` reports a size set via `remix_linux::setHeadlessClientSize`.
  The API needs a way for the mod to set it.
- **ImGui input:** the Remix menus need input forwarded from the mod (GLFW).
- `libNRD.so` (decision above).
- Linux has no share-deny file locks, so a texture another process is still writing is not detected.

---

## Hardware result: RTX 5070, reported by the user (2026-10-26)

Arch Linux, NVIDIA proprietary driver **615.78.08**, run with `DXVK_REMIX_NVIDIA_MIN_DRIVER=600`.
- **Vulkan device creation passes** on the RTX 5070 with ray tracing, opacity micromaps (OMM),
  shader execution reordering (SER) and NRC reported supported.
  The FD external memory/semaphore extensions are enabled.
- `Startup` then fails at `Presenter: window surfaces are not supported by the headless Linux runtime`,
  as designed. That is the Phase C step 1 target.
- Side items reported, not yet fixed:
  1. The minimum-driver check uses Windows driver numbering. Linux needs its own threshold, and the env override is a workaround.
  2. The DLSS libraries are not copied next to `libremix.so`.
  3. Blackwell is detected as Turing (low priority, probably from the NVAPI stub fallback).

---

## Phase C step 1: 0.16 API header and headless Startup (2026-10-26)

**Status:** compiles, links, and passes a software-Vulkan smoke test (Mesa lavapipe in the build container).
**Not verified on GPU.**

### What changed
- **ABI.** The runtime's `public/include/remix/remix_c.h` is now the mod's **0.16.0** header, plus the POSIX
  changes from `tools/port_remix_c_h.py`. `InitializeLibrary` accepts 0.16 (exact-minor rule) and fills **all 36**
  interface entries. Sizes match the header: `remixapi_Interface` 288 bytes, `remixapi_StartupInfo` 48, `remixapi_PresentInfo` 48.
- **New entry points.**
  - `Submit`, `SceneInstance*`, `CreateTextureFrom*`, `UpdateTextureFromMemory`, `DestroyTexture`,
    `UpdateMaterial` and `dxvk_GetInteropInfo` log once and return `GENERAL_FAILURE`, until steps 2 and 3 and
    the interop work. None is left null.
  - **Config layers are stubbed:** `AcquireConfigLayer` returns a distinct, reference-counted handle and
    logs that the layer is ignored. Blending and release succeed. Layer option values do not apply.
  - `GetConfigVariableResolved` is implemented for registered options, with the size-query and
    `BUFFER_TOO_SMALL` contract. Dynamic-namespace keys return `GENERAL_FAILURE`.
- **Material and dome-light textures** are `remixapi_TextureHandle`s in 0.16. They resolve through an API
  texture registry that step 3 fills, so materials currently render untextured. The 0.6.6 texture-transform
  fields are gone from the 0.16 structs, so the defaults apply.
- **Headless Startup (Linux).**
  - Always uses `D3D9SwapchainExternal` (the HdRemix-style swapchain).
  - The swapchain base never creates a `vk::Presenter` or VkSurface.
  - `Present` blits the final output into an internal back buffer and submits.
  - The external `frameEnd`/`frameResume` semaphore handshake only runs once a client calls
    `dxvk_GetExternalSwapchain`. Otherwise nothing would signal `frameResume`, and every frame would wait forever.
  - A null `hwnd` becomes a token, because ImGui and the swapchain skip their work without a window handle.
- **Output size:** new export `remixlinux_SetWindowSize(w, h)`. Use it before `Startup` and on resize; `Present`
  resets the back buffers when it changes. The fallback is `REMIX_LINUX_WINDOW_SIZE=WxH`, then 1920x1080.
  The mod's JNI bridge must call it (Phase D).

### Test (`tests/startup_remix.c`, lavapipe)
`InitializeLibrary(0.16.0)` → `remixlinux_SetWindowSize(640,360)` → `Startup` → 3× `Present(skipVulkanSwapchain=1)`
→ `Shutdown`: **all return SUCCESS** (`evidence/phaseC1-headless-lavapipe.log`).
`Shutdown` takes about 30 s on lavapipe. It waits for the background shader-pipeline prewarm
(`RtxInitializer::waitForShaderPrewarm`), which lavapipe compiles on the CPU. Expect this to be far shorter
on the 5070, but please check it.

### Semantic gaps noted, not addressed in this step
- `SetConfigVariable` writes to the User layer (0.6.6 behaviour). 0.16 routes NoSave options such as
  `rtx.showUI` to the Derived layer, and the mod's input gate relies on that, so closing the native menu may
  not stick. Revisit with the input-forwarding work.
- `PresentInfo.skipVulkanSwapchain`, `interopFlipY`, `clientDoneSemaphore_ValueToWait` and
  `StartupInfo.interopImage` are accepted and ignored until the interop work (done in Phase C step 2, below).

---

## Phase C step 2: FD interop for `dxvk_GetInteropInfo` (2026-10-26, on the RTX 5070)

**Status:** the FD mechanism itself (export, import, synchronization, readback) is **verified on the
RTX 5070** with two independent hardware proofs. A real ray-traced lit frame through it is **not yet
achieved**; see "Open issue" below. Branch `interop-local`, patch `0008-linux-dxvk_GetInteropInfo-opaque-FD-interop.patch`.

### Design

The Windows 0.16 flow (`remix_c.h`'s own comment block on `remixapi_dxvk_InteropInfo`) has `Submit`
allocate a dedicated interop image and render into it, separate from the normal D3D9 swapchain. On
Linux there is no normal swapchain at all (the runtime is headless by design, Phase B), and `Submit`
remains a stub (API-0.16-only, no public-source equivalent to port). Rather than build a parallel
render path through `Submit`, the interop image is filled from the **existing, already-working**
per-frame output: `D3D9SwapchainExternal::Present()` already produces a fully composited frame
(raytraced output blitted into the backbuffer, then GUI/HUD composited on top, then transitioned to
`SHADER_READ_ONLY_OPTIMAL`) every time a client calls `remix.Present()`. The interop path adds one more
blit of that same final image into a dedicated, separately-allocated, FD-exportable
`VK_FORMAT_R8G8B8A8_UNORM` image, then signals a semaphore on the same submission. This means the
interop image updates on **every** `Present()`, matching the documented Windows flow's cadence (step 3
happens once per frame), without touching `Submit` or the scene/texture/config-layer work at all.

Changed files:
- `src/dxvk/rtx_render/rtx_semaphore.cpp/.h`: `createTimeline` had an FD-export code path that was
  dead (it created the semaphore with `VkExportSemaphoreCreateInfo` but never actually called
  `vkGetSemaphoreFdKHR`/`vkGetSemaphoreWin32HandleKHR` to populate `m_handle`). Fixed as a general
  correctness fix; not used by the interop semaphores themselves (see below).
- `src/dxvk/dxvk_image.h`: added `DxvkImage::isShared()`, a non-consuming accessor for whether an
  image ended up exportable. `sharedHandle()` creates a fresh fd on every call (per spec, see FD
  ownership below) and would leak one if called just to probe exportability.
- `src/d3d9/d3d9_swapchain.h` / `d3d9_swapchain_external.cpp`: `D3D9SwapchainExternal` gained the
  interop color image, the two semaphores, and the blit+signal step in `Present()`.
- `src/dxvk/rtx_render/rtx_remix_api.cpp`: `remixapi_dxvk_GetInteropInfo` implemented; wires
  `StartupInfo::interopImage` through to a `g_interopImageEnabled` flag the swapchain reads (same
  pattern as the pre-existing `g_combineGuiInFinalColor`, except correctly scoped -- see pitfall below).

### Semaphores are binary, not timeline (platform-level ABI note)

The header's `dxvkDoneSemaphore_ValueToWait` / `clientDoneSemaphore_ValueToWait` fields exist because
on Windows the shared handle is (almost certainly) a D3D12 fence, importable into GL via
`GL_EXT_semaphore_win32`'s `GL_HANDLE_TYPE_D3D12_FENCE_EXT`, which is the **only** GL semaphore import
path that carries a wait/signal value (`GL_D3D12_FENCE_VALUE_EXT`). `GL_EXT_semaphore_fd` -- the only
GL path for a Linux opaque fd -- has no value parameter at all; `glWaitSemaphoreEXT`/
`glSignalSemaphoreEXT` take no value, and the imported Vulkan semaphore must be **binary**, not
timeline. So on Linux: `dxvkDoneSemaphore_Handle`/`clientDoneSemaphore_Handle` are plain binary Vulkan
semaphores (`RtxSemaphore::createBinary(..., shared=true)`), signaled with `addSignalSemaphore(h, 1)`
exactly like the pre-existing `frameEnd`/`frameResume` pair `GetExternalSwapchain` already uses.
`dxvkDoneSemaphore_ValueToWait` is reported as an informational frame counter only; a Linux client
must ignore it and just call `glWaitSemaphoreEXT`/`glSignalSemaphoreEXT` once per `Present()`, matching
binary-semaphore alternation (wait consumes the signal; don't call `Present()` again before waiting).

### FD ownership contract

- Every non-zero `*_Handle` field `dxvk_GetInteropInfo` returns is a **fresh, caller-owned fd**. The
  runtime never reuses or tracks fds it hands out; the Vulkan driver creates a brand-new fd on every
  `vkGetMemoryFdKHR`/`vkGetSemaphoreFdKHR` call for the same underlying object, exactly as the FD
  variant of `VK_KHR_external_memory`/`VK_KHR_external_semaphore` specifies.
- The caller must either import the fd (`glImportMemoryFdEXT`/`glImportSemaphoreFdEXT`, which take
  ownership and close it internally on success) or `close()` it itself if it won't be imported.
- Calling `dxvk_GetInteropInfo` again returns **new** fds for the same objects; it never invalidates
  fds already returned. Recommended client pattern, matching the header's documented flow: call it
  once right after `Startup` to import the two semaphores (valid immediately, independent of whether
  a frame has rendered yet), then once per frame after `Present()` for the color image, re-importing
  into GL only when width/height actually changed.
- Depth (`depthImageMemory_Handle` etc.) is always `0` (unavailable) on Linux for now: the primary
  depth buffer is a depth-aspect Vulkan image, and exposing it safely as a plain byte buffer to GL
  needs a format/tiling compatibility pass this step didn't attempt. `0` is a defined, legitimate value
  per the header ("or 0 if unavailable"), not a missing case.
- `dxvk_GetInteropInfo` returns `GENERAL_FAILURE` if `interopImage` wasn't set in `StartupInfo`, if
  `Startup` hasn't been called, or (image fields only) if no frame has been presented yet.

### A pitfall worth recording: file-scope globals aren't always where they look like they are

`rtx_remix_api.cpp` has two large anonymous-namespace blocks (`82..927`, `929..2093`) that sit at
**global scope**, not inside `namespace dxvk {}` as the surrounding code visually suggests (the
`namespace dxvk {}` block that's actually open at that point in the file closed at line 80). The
pre-existing `g_combineGuiInFinalColor` -- used the same way I first wrote `g_interopImageEnabled`,
as a bare `bool` definition dropped in near by -- is consequently **two separate variables**, one
with internal linkage in `rtx_remix_api.cpp`'s anonymous namespace, one with external linkage in
`dxvk::` in `d3d9_swapchain_external.cpp`; `remixapi_dxvk_CreateD3D9` setting the former never affects
the latter. `g_interopImageEnabled` was fixed by explicitly reopening `namespace dxvk { ... }` for its
definition and qualifying both use sites as `dxvk::g_interopImageEnabled`. Flagging
`g_combineGuiInFinalColor` as a likely pre-existing bug (not touched here, out of scope) for whoever
next touches `combineGuiInFinalColor`/GUI compositing.

### Verified on the RTX 5070

Driver 615.78.08, `DXVK_REMIX_NVIDIA_MIN_DRIVER=600`, PRIME offload env vars (see BUILD_LINUX.md).
`tests/interop_gl.c` (build: `tests/build_interop_gl.sh`) creates a surfaceless EGL/GL 4.5 context
pinned to the NVIDIA GPU via `EGL_EXT_platform_device` (`eglQueryDevicesEXT` + matching
`EGL_NV_device_cuda` in the device's extension string -- robust against the AMD iGPU driving the
display under PRIME, no window-system dependency), confirms `GL_VENDOR`/`GL_RENDERER` landed on the
5070, confirms `GL_EXT_memory_object_fd`/`GL_EXT_semaphore_fd` are advertised (via `glGetStringi`,
since `glGetString(GL_EXTENSIONS)` is empty on a core profile), then:

1. **`Startup(interopImage=1)` -> `dxvk_GetInteropInfo` -> import both semaphores**: returns
   `SUCCESS` with two non-zero fds (`evidence/phaseC2-interop-*-5070.log`, every run).
2. **Mechanism proof A** (`REMIX_INTEROP_DEBUG_CLEAR=1`, a since-removed one-line debug branch that
   filled the interop image directly with `vkCmdClearColorImage` to opaque magenta instead of
   blitting): `dxvk_GetInteropInfo` -> import the image via `glImportMemoryFdEXT` +
   `glTexStorageMem2DEXT` (`GL_OPTIMAL_TILING_EXT`, matching the Vulkan image's `VK_IMAGE_TILING_OPTIMAL`)
   -> `glWaitSemaphoreEXT` -> `glReadPixels`: **exact magenta (255,0,255) read back**
   (`evidence/phaseC2-interop-debugclear-image-5070.{log,ppm}`). Proves memory export/import,
   tiling/format, semaphore wait, and readback are all correct.
3. **Mechanism proof B** (`REMIX_INTEROP_DEBUG_CLEAR=2`, clearing the blit *source* -- the same
   `targetImage` the normal swapchain path uses -- to magenta immediately before the real
   `blitImageHelper` call runs): **exact magenta read back again**
   (`evidence/phaseC2-interop-debugclear-blit-5070.{log,ppm}`). Proves the blit step itself, end to
   end, through the exact code path a real frame uses.
4. **Full scene test** (`tests/interop_gl.c` as committed, no debug flag): `CreateMaterial`
   (emissive, `albedoConstant`), `CreateMesh` (a quad), `CreateLight` (sphere), `SetupCamera`
   (`CameraInfoParameterizedEXT`), 16x `{SetupCamera, DrawInstance, DrawLightInstance, Present}` ->
   `dxvk_GetInteropInfo` -> import -> wait -> read back: **all-zero** (not garbage, not noise -- a
   clean, reproducible all-black 640x360) (`evidence/phaseC2-interop-scene-5070.{log,ppm}`).

### Open issue: the test scene renders as pure black

(2) and (3) together leave only one place left for this to go wrong: Remix's renderer is writing an
all-black `targetImage` for this scene, i.e. this is a rendering/scene-setup issue, not an interop
issue. Candidates not yet ruled out, in rough order of suspicion:
- A gap specific to the **headless runtime's** handling of the classic immediate-mode API
  (`DrawInstance`/`DrawLightInstance`/`SetupCamera`) with **real** geometry -- every existing
  headless test (Phase B/C step 1) only ever exercised an *empty* scene, so this exact combination
  (classic API + a textured/lit instance + headless swapchain) has never been exercised on Linux
  before this session.
- Frame-id bookkeeping: `RtCamera::isValid()` compares against `DxvkDevice::getCurrentFrameId()`,
  which is backed by `QueuePresentCount`, incremented only by the real presenter's `presentImage()`
  -- which the headless path never calls (it bypasses `vk::Presenter` entirely, per Phase B). The
  counter is likely frozen at a constant value for the whole headless session. Traced through by hand
  this looked self-consistent (camera and check read the same frozen value), but it was not verified
  with instrumentation, and a frozen frame counter is exactly the kind of thing that breaks an
  invariant nobody had reason to check before.
- Light sampling (NEE/ReSTIR/RTXDI) needing more warmup frames than the 16 tried, though the
  emissive-material variant (self-lit, independent of light sampling) came back equally black, which
  argues against this.
- Something about the quad's winding/orientation -- tried `doubleSided=1` to rule this out, no change.

Ruled out: GL landing on the wrong GPU (checked, confirmed 5070), `GL_EXTENSIONS` query method (was a
real bug, fixed, see commit), alpha test rejecting the surface (`alphaTestType=7` is
`VK_COMPARE_OP_ALWAYS`, correct), tone mapping/auto-exposure clipping (tried `emissiveIntensity` up to
100000, still exactly zero, not just dim).

### What the cloud session needs for the Minecraft/JNI side

- `dxvk_GetInteropInfo`'s `dxvkDoneSemaphore_Handle`, `clientDoneSemaphore_Handle`,
  `imageMemory_Handle` are Linux fds (`int`, widened to `uint64_t`), not Win32 `HANDLE`s. Each is
  fresh per call and must be `close()`d or imported (which closes it) -- see "FD ownership contract".
- The semaphores are **binary**, not timeline, on Linux (see above) -- the Java/LWJGL `GlInterop`
  FD port must not attempt to set a wait/signal value on them; a bare `glWaitSemaphoreEXT`/
  `glSignalSemaphoreEXT` per frame is correct and sufficient.
- Depth is always unavailable (`depthImageMemory_Handle == 0`) for now.
- The interop image is always `REMIXAPI_TEXTURE_FORMAT_RGBA8_UNORM`, `VK_IMAGE_TILING_OPTIMAL` --
  import with `GL_TEXTURE_TILING_EXT = GL_OPTIMAL_TILING_EXT` before `glTexStorageMem2DEXT`, or the
  import will fail outright (confirmed while building the test: omitting this causes a GL error).
- Nothing about the Minecraft integration itself was touched or assumed beyond what the public
  `remix_c.h` comment block documents; the mod repo was not cloned this session (GitHub access was
  down for this tool run), so cross-check `rtxremix-java/native/remixbridge.cpp` and
  `GlInterop.java`'s actual call sites against this writeup before porting them.
