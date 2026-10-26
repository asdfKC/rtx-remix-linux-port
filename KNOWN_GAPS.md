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
| `dxvk_GetInteropInfo` depth image | dropped (for now) | `depthImageMemory_Handle` always 0. Exposing the depth-aspect image safely as a plain GL byte buffer needs a tiling/format check not yet done |
| Real ray-traced content through the FD interop image | **blocked, under investigation** | FD mechanism itself is verified on the 5070 (two independent debug-clear proofs); a test scene (material/mesh/light/camera via the classic immediate-mode API) renders all-black through it. See PORTING_NOTES.md Phase C step 2 "Open issue" |
