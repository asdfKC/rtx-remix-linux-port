# Pins

| Component | Source | Version / commit | Notes |
|---|---|---|---|
| dxvk-remix | github.com/NVIDIAGameWorks/dxvk-remix | `main` @ `bbd74a1287c2fa887f4a2b0e46a1530177890b64` (2026-10-23), API 0.6.6 | submodules at their pinned commits |
| rtx-remix-minecraft | github.com/NVIDIAGameWorks/rtx-remix-minecraft | `main` @ `f2c6fdaa763f45676f1576e63f8e054bf4ef16af` (2026-10-23), API 0.16.0 | |
| slang | github.com/shader-slang/slang | tag `v2025.10.4` (`1d8f6f86d1a6`) + cherry-picks `60a8f1fc3` (#7448), `83c7b7c77` (#7398) + local `-zero-initialize` parse fix | Remix's Packman pin is `slang-2025.10.4-39-g496fab196-win64-remix`; commit `496fab196` is not public |
| NRD | github.com/NVIDIA-RTX/NRD | `3edea46dafb29ce05c61cf839b396b291fdaddb6` (v4.13.0 WIP, 2025-02-07) | no public v4.13 tag; headers + `Shaders/` only so far. The runtime loads NRD at run time |
| DXVK native headers | github.com/doitsujin/dxvk | tag `v2.3.1` (`037d0fa1ad09`) `include/native/windows` | zlib licence |
| mingw-directx-headers | github.com/Joshua-Ashton/mingw-directx-headers | `9df86f2341616ef1888ae59919feaa6d4fad693d` | DXVK v2.3.1's pinned submodule; subset vendored |
| glslang, spirv-tools | distro | build container: Ubuntu 24.04 glslang 15.1.0, SPIRV-Tools v2025.1 | Arch equivalents: `glslang`, `spirv-tools` (extra) |

## Minecraft side (Phase D)

| What | Version | Where it is pinned |
|---|---|---|
| Minecraft | 1.21.1 | mod `build.gradle`, `scripts/install.sh` |
| Fabric Loader | 0.16.5 | mod `build.gradle`, `scripts/install.sh` |
| Fabric API | 0.116.10+1.21.1 | mod `build.gradle` (`stageFabricApi`) |
| Gradle | 9.3.1, distribution sha256 `b266d5ff…beaff06` | mod `gradle/wrapper/gradle-wrapper.properties` |
| gradle-wrapper.jar | sha256 `d3b261c2820e9e3d8d639ed084900f11f4a86050a8f83342ade7b6bc9b0d2bdd` | checked by `scripts/install.sh` |
| Runtime textures | `rtx_remix_minecraft_runtime_textures@1.1.0` (Packman) | mod `packman/textures.packman.xml` |
