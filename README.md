# rtx-remix-linux-port

Work-in-progress native Linux (x86_64, no Wine/Proton) port of NVIDIA RTX Remix for Minecraft:
the public dxvk-remix runtime, built headless on Linux, plus the Fabric mod's JNI/Java side.

**Status: Phase B (runtime builds on Linux) in progress. Nothing here has run on a GPU.**
Status words: *configures* / *compiles* / *links* / *unit tests pass* / *verified on GPU*.
Only the first two are true for parts of the tree today. See `PORTING_NOTES.md`.

## Layout

| Path | Contents |
|---|---|
| `PORTING_NOTES.md` | Phase reports, findings, decisions, gaps |
| `PINS.md` | Exact upstream commits and tags used |
| `patches/dxvk-remix/` | `git format-patch` series on dxvk-remix `bbd74a12` (branch `linux-port`) |
| `patches/slang/` | Series on shader-slang `v2025.10.4` (2 upstream cherry-picks + 1 local) |
| `tools/` | Build-log summariser and a SPIR-V bad-conversion locator used during the port |

Apply with `git am patches/<repo>/*.patch` on the pinned base.
