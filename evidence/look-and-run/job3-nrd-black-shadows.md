# Job 3: NRD/black shadows — investigated, narrowed, not solved

## Is the black permanent?

No. Standing still at the fixed view and sampling the same crop at camera-cut (0s) vs 10s
later shows a clearly noisier, more block-artifacted image right after a camera cut (`/tp`),
converging to a stable result within ~10 seconds (`job3_sparse_off.png` vs
`job3_sparse_off_converged.png` — same settings, 10 seconds apart). This matches normal
SHaRC/temporal accumulation warm-up, not a progressive darkening bug. The dark risers
discussed below are present in the *converged*, stable image, not just during warm-up.

## NRD is not the (sole) cause -- ruled out directly

Reproduced the historical "pyramid step" comparison this session (same fixed view: `0.5 80.0
0.5`, facing south, day, clear weather, matching `evidence/nrd-dlss/nrd_before_crop_pyramid.png`
/ `nrd_after_crop_pyramid.png`). Measured mean luminance (ImageMagick, grayscale) of a tight
crop isolating one shadowed step riser face in each configuration, same crop geometry
throughout:

| Configuration | Riser mean luminance (0-255) |
|---|---|
| Historical "before NRD" (from the nrd-dlss session, Ultra preset, no denoiser available) | 193.8 |
| Historical "after NRD" (same session, NRD loaded and active) | 142.9 |
| **This session, NRD active (shipped default)** | ~142 (visually matches "after NRD" above) |
| **This session, `rtx.useDenoiser = False`** (no denoising at all, NRD or otherwise) | 25.4 |
| **This session, `rtx.sparseRendering.enableSparseRendering = false`** (NRD still active) | 33.0 |

Disabling denoising entirely made the risers *darker*, not lighter, and disabling sparse
rendering (a DLSS-RR-oriented optimization flagged as a suspect in the task brief, see
`rtx.conf` comments at `rtx.sparseRendering.*`) did not recover the historical "before NRD"
brightness either (33.0, not ~194). **This directly contradicts "NRD causes the black
shadows."** Whatever is darkening these surfaces now is present with or without any denoiser,
and with or without sparse rendering.

## What actually explains the gap: not found this session

This session's "NRD active" measurement (~142) is close to the *historical* "after NRD" value
(142.9) — consistent with each other. But neither "no denoiser" (25.4) nor "no sparse
rendering" (33.0) recovers anything close to the historical "before NRD" value (193.8), even
though the historical "before" screenshot was also captured with no denoiser available
(`Unable to load NRD` at the time). Two runs that should be equivalent configurations ("no
real denoising happening") produced very different results (193.8 then vs 25.4 now) at the
same fixed view, same Ultra preset. Something else changed between that session and this one
that is not NRD, not `useDenoiser`, and not sparse rendering -- not identified this session.
Checked and ruled out as a lead: `rtx-remix-minecraft/.../confs/time/day.conf` (the mod's
time-of-day GI profile) is empty by design, so it cannot be the differentiator. Did not
bisect `rtx.conf`/runtime commits between the two sessions for other changes -- recommended
next step.

## Scope correction for a future session

The task brief's framing ("NRD makes shadows black") does not hold up under a same-session,
one-variable-at-a-time test. The real, currently reproducible problem is: **steep/vertical
surfaces facing away from the sun render with very little indirect light reaching them**
(pyramid step risers, tree canopy undersides), regardless of denoiser configuration. This
reads as a global-illumination/indirect-lighting intensity question (bounce count, SHaRC
settings, or exposure/tonemap interacting with low-signal areas), not a denoiser-specific bug.
`rtx.conf` does not override any bounce-count or indirect-lighting-intensity option (checked:
no `bounce`, `indirect`, `giBounce`, or `secondaryBounce` keys beyond
`rtx.enableIndirectTranslucentShadows`), so these areas are using the runtime's own defaults.

**Recommended next steps for a future session** (not attempted, out of this session's time):
1. Git-bisect `rtx.conf` and the runtime/mod commits between the nrd-dlss session's "before
   NRD" screenshot and now, to find what actually changed GI brightness in shadowed areas.
2. Compare `rtx.localtonemap.shadows` / `rtx.autoExposure.*` (already present in `rtx.conf`,
   tuned with a `-0.75 EV bias` comment) against a config with these reset to engine defaults,
   isolating tonemap/exposure from GI intensity.
3. Get a clean, fresh un-tuned `rtx.conf` (strip the mod's authored baseline entirely) and
   compare the same crop, to separate "this Linux port's behavior" from "this mod's authored
   look."

## Decision

No code or settings change applied this session for Job 3 -- the two tested hypotheses
(denoiser, sparse rendering) were both ruled out with direct evidence, and the actual cause
needs the git-bisection above, which didn't fit in the remaining time budget. `rtx.conf` was
restored to its shipped defaults (`rtx.sparseRendering.enableSparseRendering = true`,
`rtx.useDenoiser` unset/default `true`) before moving on.

## Files

- `evidence/look-and-run/job3_nrd_on_t0.png` through `_t60.png` -- stand-still sequence,
  confirms no progressive darkening.
- `evidence/look-and-run/job3_sparse_off.png` (camera-cut, noisy) and
  `job3_sparse_off_converged.png` (10s later, stable) -- same settings, shows accumulation
  warm-up is not the dark-riser cause.
- `evidence/nrd-dlss/nrd_before_crop_pyramid.png`, `nrd_after_crop_pyramid.png` -- the
  historical comparison this session re-tested and could not reproduce the "before" result
  for.
