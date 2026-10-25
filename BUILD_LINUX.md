# Building the Linux runtime (Arch Linux, x86_64)

> **Status:** work in progress. These steps produce `libremix.so` once Phase B is complete.
> Nothing here has been run on a GPU. See `PORTING_NOTES.md` for the current state.

## 1. Packages (official repos only)

```sh
sudo pacman -S --needed base-devel git cmake ninja meson python \
  glslang spirv-tools vulkan-headers vulkan-icd-loader ttf-dejavu
```

| Package | Why |
|---|---|
| base-devel, cmake, ninja, meson, python | Toolchain and build systems (dxvk-remix uses meson, slang uses CMake) |
| glslang, spirv-tools | `glslangValidator` and `spirv-val` for the shader build |
| vulkan-headers, vulkan-icd-loader | Vulkan headers and `libvulkan.so`. The NVIDIA driver provides the ICD |
| ttf-dejavu | Stand-in fonts for the Remix menus (see below) |

No AUR packages are used.

## 2. Build

```sh
git clone https://github.com/asdfKC/rtx-remix-linux-port
cd rtx-remix-linux-port
scripts/build-runtime.sh /mnt/big/rtx-remix-linux
```

The script works under the directory you give it (default `/mnt/big/rtx-remix-linux`) so the root filesystem
stays free. It needs about 15 GB. Steps:

1. Clones **shader-slang** at `v2025.10.4`, applies `patches/slang/` (two upstream cherry-picks and one
   flag fix), and builds `slangc`.
2. Clones **NRD** at the v4.13.0 commit, for headers and shader includes.
3. Clones **dxvk-remix** at the pinned commit with its submodules, and applies `patches/dxvk-remix/`.
4. Sets up fonts. By default it uses DejaVu as a stand-in for NVIDIA's font package.
   `REMIX_FONTS=packman` instead fetches the real `rtx-remix-fonts@2` through dxvk-remix's own Packman script.
   That script downloads NVIDIA's Packman bootstrap Python and the font archive from NVIDIA's CDN.
5. Runs `meson setup` and `ninja`. The output is `build/src/d3d9/libremix.so`.

## 3. Not yet covered

- `libNRD.so` (the NRD denoiser library). The runtime loads it at run time and disables NRD if it is
  missing. Building it needs NVIDIA-RTX/MathLib, NVIDIA-RTX/ShaderMake and DXC
  (`directx-shader-compiler` in extra). That decision is still pending.
- DLSS libraries come from `dxvk-remix/submodules/dlss/lib/Linux_x86_64/rel/`. The install step will copy
  them next to `libremix.so`.
