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
  `StartupInfo.interopImage` are accepted and ignored until the interop work.

### Verified on GPU: step 1 (reported by the user, 2026-10-26)
RTX 5070, Arch Linux, NVIDIA driver **615.78.08**, `DXVK_REMIX_NVIDIA_MIN_DRIVER=600`.
`tests/startup_remix.c` returned `Startup`, 3× `Present` and `Shutdown` all **0x0**, with no surface error.
The device came up with RT, OMM, SER, NRC and the FD external memory/semaphore extensions enabled.
The run took 14.3 s in total, about 13.5 s of it in `Shutdown`, with a cold shader cache.

## Phase C step 2: retained scene instances, Submit, UpdateMaterial (2026-10-26)

**Status:** compiles, links, and passes a software-Vulkan smoke test (lavapipe). **Not verified on GPU.**

### Behaviour, taken from the mod (`remixbridge.cpp`, `RemixInstance.java`, `RemixRenderer.java`)
- The mod adds an instance once and calls `SceneInstanceUpdate` only when something changes. Every update is a
  complete replacement. Unchanged instances make **no** native call, so they must keep rendering every frame.
  Culled instances are removed, and the mod keeps `INACTIVE_HANDLE` until they come back.
- The `pNext` chain (`InstanceInfoBlendEXT` on the C heap) is only valid **during** the call.
- The mod calls `Submit` once per frame, before `Present`. `blitGlToRemix` calls it if the frame has not done so yet.
- `sceneInstanceAdd` treats a 0 handle as failure.

### Implementation (`rtx_remix_api.cpp`)
- **Handles.** Handles come from a counter on the caller thread (never 0, never reused), so `Add` returns one
  at once. The caller thread also keeps a set of live handles (under the API mutex), so `Update` and `Remove`
  on an unknown or removed handle return `INVALID_ARGUMENTS` straight away.
- **Instance state.** `InstanceInfo` is converted to `ExternalDrawState` during the call, the same conversion
  `DrawInstance` uses, and that consumes the `pNext` chain. A CS command then stores it in a map on the CS
  thread. `Remove` erases it the same way. The CS ordering keeps add, update and remove in call order.
- **Frame boundary.** `Submit` emits one CS command. It runs each stored instance through the existing
  `DrawInstance` path (`commitExternalGeometryToRT`). If a frame reaches `Present` without a `Submit`, the replay
  happens there, so retained instances never drop out for a frame. It happens at most once per frame.
- **`UpdateMaterial`** replaces the material data for the handle on the CS thread
  (`AssetReplacer::updateExternalMaterial`). Draws look up materials by handle at draw time, so live instances
  pick up the change on the next frame. An update to a handle that was never created registers it and logs a warning.
- **`Shutdown`** drops all retained instances after the device is released. Instances still alive at that point are not an error.
- **Debug export (Linux):** `remixlinux_GetRetainedSceneStats(&live, &replayedDraws)` returns the live count
  and the total number of draws replayed.

### Test (`tests/scene_remix.c`, lavapipe; `evidence/phaseC2-scene-lavapipe.log`)
The test runs `Startup` → `CreateMaterial` → `CreateMesh` (one quad) → `SceneInstanceAdd`, then overwrites the
info struct. Then:
- 4 frames (`SetupCamera` + `Submit` + `Present`), then 1 `Present` with no `Submit`.
- `SceneInstanceUpdate` (moved), `UpdateMaterial` (new albedo), then 2 frames.
- `SceneInstanceRemove`, then 5 frames.
- Error paths, a second instance left alive, `Shutdown`.

Every call returned what was expected. The stats read `live=1 replayedDraws=4` after the first 4 frames, then
`live=0 replayedDraws=7` after the remove. The count did not grow in later frames.
The test checks that calls succeed and that the replay is counted. It does not check pixels: the headless
runtime has no readback yet, and that needs the interop image.

### Not covered yet
- An instance whose mesh is destroyed stays live. It draws nothing, because `accessExternalMesh` returns an
  empty mesh for unknown handles, and it does not crash. If a later mesh got the same handle value (handles are
  heap pointers), the instance would draw that mesh. The mod's entity submitter destroys transient meshes only after
  no instance refers to them (`retireTransientMeshes`). The mod's other destroy paths have not been checked.
- `SubmitInfo` has no fields the runtime uses yet. In 0.16 the frame should render into the interop image;
  that is part of the FD interop work.
