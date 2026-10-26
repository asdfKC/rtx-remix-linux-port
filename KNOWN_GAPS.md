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
| NVAPI GPU-architecture detection | stubbed | Windows driver API. Presets fall back to non-NVAPI defaults |
| USD mods, USD game capture, USD-authored lights/materials | dropped | USD compiled out (approved decision) |
| Aftermath GPU crash dumps, Sentry crash reporting | dropped | Windows-only SDKs |
| RTX IO (GPU texture decompression) | dropped | Windows-only |
| Texture/conf hot reload (file watch) | dropped | Win32 `ReadDirectoryChangesW`. inotify is possible later |
| OpenVR/OpenXR | inert | Registry/Wine based |
| NRD denoiser | pending | `libNRD.so` not built yet (needs MathLib, ShaderMake, DXC) |
| Remix presenting its own window | dropped by design | Headless runtime. GL presents (`skipVulkanSwapchain = 1`) |
| Remix menu input | pending (Phase C/D) | Needs input forwarding from the mod |
| Atlas UV-bounds wrapping, dome light opacity/blend (API 0.16 fields) | pending | Phase C |
| Shader parity with NVIDIA's internal slang | unknown | Public slang + 2 cherry-picks; NVIDIA's 39 private commits are not public |
| Fast Shutdown | open | `Shutdown` waits for the background shader-pipeline prewarm to finish (`RtxInitializer::waitForShaderPrewarm`, about 13 s on the 5070 with a cold cache, about 30 s on lavapipe). The pipeline compile workers cannot be cancelled, so there is no simple fix. Workaround: `RTX_ASYNC_SHADER_PREWARMING=0` makes Shutdown fast, but pipelines then compile on first use, which causes stutters. A warm driver shader cache should shorten it |
| Destroying a mesh that live scene instances use | not handled | Such an instance draws nothing and does not crash (see PORTING_NOTES step 2) |
| Texture mip regeneration on update | whole chain | `UpdateTextureFromMemory` regenerates every mip of the patched texture, not only the patched rect. The cost scales with texture size (an animated 4096² atlas is about 22 M texels per update) |
| Mips of octahedral normal maps | approximate | Mips average the encoded values. Fine for the mod's 8-bit maps, not renormalised |
| sRGB interpretation of RGBA8_UNORM color textures | open question | Sampled as linear per the 0.16 header. `REMIX_LINUX_COLOR_UNORM_AS_SRGB=1` switches it for comparison on GPU |
| `CreateTextureFromFile` decode errors | asynchronous | A missing path fails straight away. A file that exists but does not decode gives a valid, empty (untextured) handle and an error in the log |
| Materials created before their textures | untextured | Texture handles resolve when the material is created on the CS thread. The mod creates textures first |
| `dxvk_GetInteropInfo` depth image | dropped (for now) | `depthImageMemory_Handle` always 0. Exposing the depth-aspect image safely as a plain GL byte buffer needs a tiling/format check not yet done |
| First-use shader compilation can silently skip a frame | behavioral note, not a gap | `rtx.shader.enableAsyncCompilation` (`RTX_ENABLE_ASYNC_COMPILATION`, default true) makes `injectRTX` skip ray tracing with no log line while a newly-needed shader pipeline is still compiling. Not Linux-specific, but worth knowing when driving this runtime from a fresh process with a scene whose shaders have never been compiled before (see PORTING_NOTES.md Phase D step 5) |
