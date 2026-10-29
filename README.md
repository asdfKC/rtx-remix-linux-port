# rtx-remix-linux-port

Work-in-progress native Linux (x86_64, no Wine/Proton) port of NVIDIA RTX Remix for Minecraft:
the public dxvk-remix runtime, built headless on Linux, plus the Fabric mod's JNI/Java side.

**Status: first path-traced frame verified on GPU** (RTX 5070, driver 615.78.08, 2026-10-28, by Kush). In the
Creative "New World" save, the interop color texture dumped through the real GL import path shows path-traced
geometry (tree, dug-out pit, grass, soft shadows, GI) at 30 s, 2 min and 5 min after it first appeared, matching
the on-screen screenshots. Evidence: `evidence/fix-verify-2026-10-28/`; the investigation that led there is in
`evidence/live-test-2026-10-26/`.

That is all that is verified so far. Not verified: colour space, exposure, texture correctness, weather,
performance. See `KNOWN_GAPS.md` and `PORTING_NOTES.md`.

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
