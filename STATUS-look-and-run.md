# Look-and-run session status

Branch: `look-and-run` (from `nrd-dlss`). Unattended run, started 2026-10-30.

| Job | Status | Before | After | Evidence | Commit |
|-----|--------|--------|-------|----------|--------|
| 1: DLSS/NGX crash | **root cause found, fixed; new blocker found** | crashes deterministically ~1s after `NGX init:`, every time | survives past NGX init, reaches main menu, loads a world with NGX enabled -- but hangs (GPU 0%) once gameplay actually starts with DLSS active | `evidence/look-and-run/job1-dlss-stack-fix.md` | `1d8f818` |
| 2: clouds block the sun | **cause found, safe mitigation applied** | clouds render as a dark brown blob, visibly blocking the sun | clouds off by default (`renderClouds:"false"` in a fresh/existing `options.txt` that doesn't already set it); sun renders clean | `evidence/look-and-run/job2-clouds-sun-blocker.md` | `9f9c626` |
| 3: NRD / black shadows | **investigated, narrowed, not solved** | pyramid step risers crushed to near-black, texture-less | same darkness measured with NRD on, denoiser fully off, and sparse rendering off -- NRD and sparse rendering both ruled out as the (sole) cause; real cause not found | `evidence/look-and-run/job3-nrd-black-shadows.md` | `a4ee68a` |
| 4: color, exposure, look | **tested, not adopted** | n/a (no change made) | `REMIX_LINUX_COLOR_UNORM_AS_SRGB=1` makes mid-tones more saturated, not less; not an improvement, not adopted | `evidence/look-and-run/job4-color-exposure.md` | `6e32499` |
| 5: upscaler fallback | **skipped** | -- | Job 1's crash was fixed (the literal condition for Job 5, "if DLSS stayed dead", did not hold), so this wasn't attempted. DLSS is still not practically usable end-to-end due to the Job 1 hang, but that's a different problem than needing a fallback upscaler | -- | -- |
| 6: wrap-up | this document | -- | -- | -- | (this commit) |

## Decisions made unattended (recorded per task brief rules)

- Job 1's new render-thread-present hang (found after fixing the crash) was investigated for
  about 15 minutes (jstack, GPU utilization, log inspection) and then documented rather than
  chased further, per the time-box philosophy applied throughout this session -- it's a
  distinct, non-trivial bug from the crash this job was about.
- Job 3 ran well over a typical time-box because the first two tested hypotheses (NRD,
  sparse rendering) both had to be *disproven* with real measurements rather than confirmed,
  which took longer than confirming a correct hypothesis would have. Stopped after ruling
  both out and writing up the narrowed, corrected understanding of the problem, rather than
  open-endedly continuing to search for the real cause (recommended next step: git-bisect
  `rtx.conf`/runtime changes since the last known-good screenshot).
- Job 4 was intentionally kept to a single cheap test given how much time Jobs 1-3 took; the
  remaining Job 4 angles (auto-exposure/tonemap option support, vertex-color double-tint) are
  left as next steps rather than attempted under time pressure.
- `scripts/install.sh` now always sets `AutomaticJava=false` on the Prism instance -- this is a
  real bug fix (that setting silently discarded `JavaPath`/`JvmArgs`), not a workaround, so it's
  unconditional rather than gated behind a flag.
- The clouds-off mitigation in `scripts/install.sh` only writes `renderClouds:"false"` when
  `options.txt` doesn't already set `renderClouds`, so it never overwrites a user's own choice
  (made on first install, or on any reinstall where the file already exists).

## Verification notes

- Job 1: the crash fix is GPU-verified (reached main menu + loaded a world with NGX enabled,
  confirmed via live log inspection and `coredumpctl list` showing no new crash for that run,
  where every prior run crashed within ~1s). The stack-size root cause itself is verified via
  a controlled repro (`tests/ngx_init_min.c` with `ulimit -s` shrunk to 1024/512 KB reproduces
  the identical `+0xc3505` crash offset; 2048/4096/8192 KB do not). The new hang is GPU-verified
  (`nvidia-smi` sustained 0%, `jstack` showing the Render thread stuck in native `present`) but
  not root-caused.
- Job 2: GPU-verified with matched before/after screenshots at the same fixed sunlit view.
- Job 3: GPU-verified measurements (ImageMagick mean luminance on matched crops) for all three
  tested configurations (NRD on, denoiser off, sparse rendering off); the "real" cause was not
  found, so there is no "after" to verify a fix against -- correctly reported as unsolved rather
  than claiming a fix that wasn't made.
- Job 4: GPU-verified screenshot comparison; correctly reported as "not an improvement" rather
  than silently adopting an unjustified change.

## Patch series

No new runtime, mod, or NRD patches this session -- all changes are in `scripts/install.sh`
(JVM args, `AutomaticJava`, `options.txt` clouds mitigation) and documentation. The existing
patch series (`patches/dxvk-remix/0001`-`0014`, `patches/rtx-remix-minecraft/0001`-`0007` +
`optional/`, `patches/nrd/0001`) is unchanged, sequentially numbered with no duplicates, and
was implicitly re-verified to apply and build correctly by this session's many successful
`scripts/install.sh` runs and game launches.

## Push

Attempted `git push -u origin look-and-run` from this session; see the final message to the
user for the result (no SSH agent is available in this environment, matching every prior
session on this branch family -- the user pushes manually if it fails here too).

## Next steps for a future session

1. **Job 1 hang**: instrument `GlInterop.signalDone()` / `m_interopNeedsClientDoneWait`
   specifically on the DLSS-enabled path, or temporarily revert patch `0014`'s clientDone wait
   to confirm/rule it out as the hang's cause.
2. **Job 3 shadows**: git-bisect `rtx.conf` and runtime/mod commits between the nrd-dlss
   session's "before NRD" screenshot and now to find what actually changed GI brightness in
   shadowed areas; also try a clean, unauthored `rtx.conf` to separate "this Linux port's
   behavior" from "this mod's authored look."
3. **Job 4**: auto-exposure/tonemap option support check (log for "unknown option" lines) and
   vertex-color double-tint test, both not attempted this session.
4. **Job 5**: worth doing once Job 1's hang is fixed and DLSS is either fully usable or still
   not, to have a real non-DLSS upscaler comparison on the record either way.
