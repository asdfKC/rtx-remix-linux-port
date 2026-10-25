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
