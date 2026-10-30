# NRD + DLSS session status

| Job | Status | Before | After | Evidence | Commit |
|-----|--------|--------|-------|----------|--------|
| A: NRD denoiser | **built, shipped, verified** | log: `Unable to load NRD, this feature will be disabled`; 92 fps / GPU 95-100% at fixed view, no denoising | log: clean load (no NRD errors); 81 fps / GPU 82% at fixed view; visibly reduced shadow grain (see crop comparison) | `evidence/nrd-dlss/nrd-built.md` | `4acda9c` |
| B: DLSS/NGX crash | **investigated further, not solved** | Phase 4: driver-side crash, root cause unknown, no repro | minimal standalone repro built; does NOT reproduce the crash even matching the real call in full detail (extensions, RT/sync2/BDA features, queues, API version, RTLD_LOCAL hosting) — narrows cause to something process-specific, not a bare driver bug | `evidence/nrd-dlss/ngx-bug-report.md` | (this commit) |

## Decisions made unattended (recorded per task brief rules)

- NRD's upstream `CMakeLists.txt` pinned ShaderMake/MathLib to floating `main`, which had
  drifted and broke the build (removed `--useAPI`, renamed `float4x4` members). Fixed by
  pinning both to the commit each repo was at around NRD's own Feb 2025 pin date, tracked as
  a proper patch (`patches/nrd/0001-...patch`) applied the same way as the existing
  dxvk-remix/slang patch series, rather than editing the checkout ad hoc.
- NRD denoiser staging is automatic (auto-detects `<work>/NRD/build/libNRD.so`), matching how
  `libremix.so` itself is staged — no new `install.sh` flag, to keep the "NRD always ships
  when built" behavior simple and consistent with how the runtime library works.
- For Job B, after the minimal repro didn't crash in any of 5 increasingly-matched variants,
  stopped per the time-box rather than attempting process-level theories (thread-context
  mimicry, DXVK's own Vulkan loader wrapper, Steam layer suppression) that would need
  significantly more investigation time. Documented as clear next steps instead.
- NGX/DLSS stays off by default (`REMIX_LINUX_ENABLE_NGX` unset) — the instance was restored
  to that state before finishing (it was temporarily opted in with `--dlss` to capture a fresh
  crash log for extension/feature comparison).

## Verification notes

- NRD: verified via log (no `Unable to load NRD` lines), `nm -D` symbol check (all 9 exports
  present), and before/after screenshots at the fixed test view showing reduced shadow grain.
  FPS cost measured directly (92→81 fps) at the same view/preset as the Phase 2 baseline.
- DLSS: the crash itself was re-confirmed this session (same `+0xc3505` fault, same log
  truncation right after "NGX init" prints) before building the repro. The repro is novel work
  this session — Phase 4 recommended but didn't build it.

## Branch

`nrd-dlss`, based on `quality-pass` (8 commits, ends `5d8e540`). Not pushed by this session —
push access status unknown (not attempted; ask the user to push, or confirm SSH agent access
for a future session).
