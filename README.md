# rtx-remix-linux-port

Work-in-progress native Linux (x86_64, no Wine/Proton) port of NVIDIA RTX Remix for Minecraft:
the public dxvk-remix runtime, built headless on Linux, plus the Fabric mod's JNI/Java side.

**Status: runs and renders in the real game on an RTX 5070** (driver 615.78.08, verified 2026-10-28 through
2026-10-30, by Kush). Ultra preset, 92 fps / GPU 99-100% at a fixed test view with NRD denoising off, 81 fps / GPU
82% with NRD on. The DLSS/NGX startup crash is root-caused and fixed (see `KNOWN_GAPS.md`), but DLSS is still off
by default: enabling it now gets past the crash but hangs once gameplay starts, not yet solved. Clouds are off by
default (mitigation for a visual bug, see `KNOWN_GAPS.md`). Colour space, exposure, and some shadowed-area darkness
issues remain unverified/unsolved. See `KNOWN_GAPS.md` and `PORTING_NOTES.md` for the full picture, and
`evidence/look-and-run/` for the most recent session's screenshots and findings.

Status words: *configures* / *compiles* / *links* / *runs on lavapipe* / *unit tests pass* / *verified on GPU*.
Only the user, with logs or captures from the GPU, can mark something *verified on GPU*.

## Layout

| Path | Contents |
|---|---|
| `PORTING_NOTES.md` | Phase reports, findings, decisions, gaps |
| `KNOWN_GAPS.md` | What is dropped, stubbed, pending or unverified on Linux |
| `BUILD_LINUX.md` | Build and install steps (Arch, official packages only) |
| `PINS.md` | Exact upstream commits, tags and versions used |
| `patches/dxvk-remix/` | `git format-patch` series on dxvk-remix `bbd74a12` |
| `patches/rtx-remix-minecraft/` | Series on rtx-remix-minecraft `f2c6fda`. `optional/` holds the diagnostics patch, applied only with `install.sh --diagnostics` |
| `patches/slang/` | Series on shader-slang `v2025.10.4` (2 upstream cherry-picks + 1 local) |
| `scripts/` | `build-runtime.sh` (runtime), `install.sh` (mod, Prism instance, launch script) |
| `tests/` | C and Java smoke tests for the runtime API, the bridge and the interop protocol |
| `evidence/` | Logs, dumps and screenshots behind every status claim |
| `tools/` | Build-log summariser and a SPIR-V bad-conversion locator used during the port |

Apply a series with `git am patches/<repo>/*.patch` on its pinned base.

## Running it

`scripts/install.sh` builds the mod and sets up the Prism instance. It also writes `~/.local/bin/remix-minecraft`,
which exports the environment the runtime needs (`DXVK_REMIX_NVIDIA_MIN_DRIVER=600`,
`RTX_ENABLE_ASYNC_COMPILATION=0`, the NVIDIA PRIME offload variables) and runs `prismlauncher -l rtx-remix-linux`.
Start the game with that script while Prism is closed. Prism's WrapperCommand setting did not pass environment
variables to the game.
