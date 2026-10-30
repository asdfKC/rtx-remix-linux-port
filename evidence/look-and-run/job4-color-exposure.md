# Job 4: color, exposure, look -- cheap test only, no change adopted

Time-boxed given how much of the session Jobs 1-3 took. Ran the one cheap, reversible test
from the task brief and stopped there rather than reverse-engineering the color pipeline
blind.

## `REMIX_LINUX_COLOR_UNORM_AS_SRGB=1`

Tested at the fixed view (day, clear weather) per `PORTING_NOTES.md`'s existing note that this
env var samples 8-bit UNORM albedo/emissive textures as sRGB instead of linear, for washed-out
colors. Screenshot: `job4_srgb_on_fixed.png`, compare against the same view without it
(`job3_nrd_on_t60.png`, `job3_sparse_off_converged.png`).

Observed: sand goes from pale cream to a more saturated orange-tan; grass is not visibly
different. This is **not** the "sky stops blowing out, grass stops looking neon" improvement
the task brief was checking for -- if anything, it pushes mid-tones toward more saturation,
not less. With no Windows reference screenshot available to judge which interpretation
(linear vs sRGB decode of the UNORM atlases) is actually correct, and the observed change
not being an unambiguous improvement, **not adopted**. `REMIX_LINUX_ENABLE_NGX` and friends
stay the only documented opt-in env vars; this one remains documented as available in
`PORTING_NOTES.md` but off by default, unchanged from before this session.

## Not attempted this session (time-boxed out)

- Auto-exposure/tonemap option comparison against what 0.6.6 actually supports (checking the
  log for "unknown option" lines on the mod's full `rtx.conf`).
- Vertex-color/biome-tint double-application test (`rtx.vertexColorStrength`,
  `rtx.vertexColorIsBakedLighting` toggles).

Both are reasonable next steps for a future session with more time, following the same
one-variable-at-a-time, before/after-screenshot method used in Jobs 1-3.

## Decision

No `rtx.conf`, launch script, or runtime change made for Job 4.
