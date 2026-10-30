# Known gaps (native Linux runtime)

Kept current per phase. "Dropped" means not available in the Linux build. "Stubbed" means the code path
exists but reports unsupported.

| Feature | Status on Linux | Reason |
|---|---|---|
| DLSS Frame Generation / multi-frame generation | out of scope (code present, unverified) | No validated Linux path. HW-scheduling check is WDDM-only |
| DLSS-NR (neural rendering) | dropped | Headers only in the Windows NGX Packman package |
| NVIDIA Reflex | dropped | Windows-only SDK |
| Neural Radiance Cache (NRC) | stubbed | Windows-only binaries |
| Intel XeSS | stubbed | Windows-only binaries |
| NVAPI GPU-architecture detection | stubbed, Vulkan-deviceID fallback added | NvAPI is Windows-only so `NvAPI_Initialize()` always fails on Linux; `RtxOptions::getNvidiaArch()`/`getNvidiaChipId()` now fall back to a Vulkan-deviceID-range table (Turing/Ampere/Ada/Blackwell, patch `0013`) instead of hardcoding Turing, fed from `VkPhysicalDeviceProperties::deviceID`. Verified on an RTX 5070 (GB205, deviceID `0x2f04`): log now reports Blackwell/Ultra instead of Turing/Low |
| USD mods, USD game capture, USD-authored lights/materials | dropped | USD compiled out (approved decision) |
| Aftermath GPU crash dumps, Sentry crash reporting | dropped | Windows-only SDKs |
| RTX IO (GPU texture decompression) | dropped | Windows-only |
| Texture/conf hot reload (file watch) | dropped | Win32 `ReadDirectoryChangesW`. inotify is possible later |
| OpenVR/OpenXR | inert | Registry/Wine based |
| NRD denoiser | not built, blocked on one `sudo` command | `libNRD.so` not built. No runtime code change needed: it already dlopen()s `libNRD.so` correctly and version-checks cleanly (NRD's `Include/NRD.h` is symlinked from this machine's NRD 4.13.0 checkout, so there is zero drift risk). NRD's own CMake is already Linux/Clang-ready; it just needs a DXC binary with SPIR-V output, which Arch ships officially as `extra/directx-shader-compiler` -- installing it needs an interactive `sudo pacman -S directx-shader-compiler` this unattended session could not provide. See `evidence/quality-pass/phase3-nrd-blocked.md` for the exact resume steps. With NGX off too, frames have no NRD or DLSS-RR denoising |
| Remix presenting its own window | dropped by design | Headless runtime. GL presents (`skipVulkanSwapchain = 1`) |
| Remix menu input | pending (Phase C/D) | Needs input forwarding from the mod |
| Atlas UV-bounds wrapping, dome light opacity/blend (API 0.16 fields) | pending | Phase C |
| Shader parity with NVIDIA's internal slang | unknown | Public slang + 2 cherry-picks; NVIDIA's 39 private commits are not public |
| Fast Shutdown | open | `Shutdown` waits for the background shader-pipeline prewarm to finish (`RtxInitializer::waitForShaderPrewarm`, about 13 s on the 5070 with a cold cache, about 30 s on lavapipe). The pipeline compile workers cannot be cancelled, so there is no simple fix. Workaround: `RTX_ASYNC_SHADER_PREWARMING=0` makes Shutdown fast, but pipelines then compile on first use, which causes stutters. A warm driver shader cache should shorten it |
| Destroying a mesh that live scene instances use | not handled | Such an instance draws nothing and does not crash (see PORTING_NOTES step 2) |
| Texture mip regeneration on update | whole chain | `UpdateTextureFromMemory` regenerates every mip of the patched texture, not only the patched rect. The cost scales with texture size (an animated 4096² atlas is about 22 M texels per update) |
| Mips of octahedral normal maps | approximate | Mips average the encoded values. Fine for the mod's 8-bit maps, not renormalised |
| Colour space and exposure | unverified | RGBA8_UNORM color textures are sampled as linear per the 0.16 header (`REMIX_LINUX_COLOR_UNORM_AS_SRGB=1` switches it). The first verified frames show path-traced geometry, but nobody has yet compared colours or exposure with the Windows build |
| `CreateTextureFromFile` decode errors | asynchronous | A missing path fails straight away. A file that exists but does not decode gives a valid, empty (untextured) handle and an error in the log |
| Materials created before their textures | untextured | Texture handles resolve when the material is created on the CS thread. The mod creates textures first |
| `dxvk_GetInteropInfo` depth image | dropped (for now) | `depthImageMemory_Handle` always 0. Exposing the depth-aspect image safely as a plain GL byte buffer needs a tiling/format check not yet done |
| First-use shader compilation can silently skip a frame | behavioral note, not a gap | `rtx.shader.enableAsyncCompilation` (`RTX_ENABLE_ASYNC_COMPILATION`, default true) makes `injectRTX` skip ray tracing with no log line while a newly-needed shader pipeline is still compiling. Not Linux-specific, but worth knowing when driving this runtime from a fresh process with a scene whose shaders have never been compiled before (see PORTING_NOTES.md Phase D step 5) |
| DLSS / DLSS-RR (NGX) on Linux | off by default | `NVSDK_NGX_VULKAN_Init_Ext2` faults inside the driver's `libnvidia-ngx.so.1` (+0xc3505) with the DLSS libraries present, also with patch 0011. Deterministic across runs (identical crash offset every time). Ruled out this session: missing Vulkan extensions (all 5 extensions `NVSDK_NGX_VULKAN_RequiredExtensions` asks for are confirmed enabled), a stale/mismatched NGX library (confirmed loading the exact driver-matched `libnvidia-ngx.so.615.78.08`), and NGX's own internal logging never fires before the fault (tested with `NVSDK_NGX_LOGGING_LEVEL_VERBOSE` forced on). Root cause is inside the closed-source driver; see `evidence/quality-pass/dlss-investigation.md`. Opt in with `REMIX_LINUX_ENABLE_NGX=1` (runtime) and `--dlss` (install.sh) / `-PlinuxDlss=true` (Gradle) |
| Interop read/write ordering on Linux | fixed | ~~The runtime does not wait on clientDone before blitting into the interop image~~ -- fixed (patches `0014` runtime + `0006` mod): the runtime now waits on the clientDone semaphore before each blit (skipped only on the first blit of an image's lifetime), and the mod signals it instead of calling `glFinish()` every frame. Measured on an RTX 5070 at a fixed view/preset: 58fps/GPU 71-75% -> 92fps/GPU 99-100% (+59%) |
| Interop image import failure on Linux | not recovered | If GL fails to import the image, the runtime keeps signalling dxvkDone, which nothing waits for; the next signals are invalid binary-semaphore use |
| Rain and weather | skipped | The weather renderer needs the primary depth image through interop, and `depthImageMemory_Handle` is always 0 on Linux. Mod patch 0005 guards against the missing depth (logs once) instead of crashing; weather stays off until depth interop exists |
| Game environment through Prism | worked around | Prism's WrapperCommand did not pass environment variables to the game (checked in /proc/<pid>/environ). `install.sh` writes a launch script that exports them and runs `prismlauncher -l`; starting from Prism's own window skips them |
