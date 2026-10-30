# Job 2: clouds act like opaque sun blockers

## Confirmed: Clouds OFF fixes it, Clouds ON is the cause

- `renderClouds:"fast"` (shipped default) + sunlit view (noon, clear weather, looking toward
  the sun): the cloud renders as a dark brown/gray blob, and the sun behind it is dimmed to a
  soft hazy glow instead of a bright clean disc. Screenshot:
  `job2_clouds_on_sunlit.png`.
- Same view, `renderClouds:"false"`: the sun renders as a bright, sharp-edged disc with no
  obstruction. Screenshot: `job2_clouds_off_sunlit.png`.
- A separate ground-level view at a low sun angle (shortly after `/time set day`) shows every
  non-sun-facing surface (tree canopies, hillsides, even grass right next to the player)
  rendered fully black, consistent with the scene's only light source (the sun) being
  significantly attenuated: `job2_clouds_on_shadows.png`.

## Root cause: how the mod submits clouds, and how Remix's material system treats them

`rtx-remix-minecraft/src/main/java/com/nvidia/rtxremix/minecraft/cloud/CloudRenderer.java`
builds one material for all clouds (`createMaterialInfo`, line ~541):

```java
MaterialInfoOpaqueSubsurfaceEXT subsurface = MaterialInfoOpaqueSubsurfaceEXT.builder()
    .diffusionProfile(true)
    .transmittanceColor(0.999f, 0.999f, 0.999f)
    .radius(0.98f, 0.98f, 0.98f)
    ...
MaterialInfoOpaqueEXT opaque = MaterialInfoOpaqueEXT.builder()
    .albedoConstant(red, green, blue)
    ...
    .subsurfaceExt(subsurface)
    .build();
return MaterialInfo.builder().opaqueExt(opaque).build();
```

This is `MaterialInfoOpaqueEXT` (Remix's **opaque** material family), with a subsurface-
scattering extension attached. It is not `MaterialInfoTranslucentEXT` (the family that actually
lets light transmit through, with real alpha/refraction). Checked the runtime's material
pipeline (`rtx_remix_api.cpp`, `rtx_materials.h`, `rtx_options.h`,
`integrator_direct.slangh`) -- this is **shared upstream Remix code**, not something the Linux
port changed or is missing:

- `rtx.subsurface.enableDiffusionProfile` defaults to `true` (`rtx_options.h:917`), so the
  subsurface diffusion profile is active by default; this is not a disabled/missing feature on
  this build.
- The direct-lighting integrator (`integrator_direct.slangh`) does have specific subsurface
  handling (`visibilityModeEnableSubsurfaceMaterials`, `isDiffusionProfileSss`) -- but subsurface
  scattering approximates light re-emerging *near where it entered the same surface*, which is
  a local shading effect, not a translucency/transmission BSDF that lets light pass through to
  illuminate what's behind the surface. A shadow ray still treats the cloud as a (mostly)
  opaque occluder.
- The mod's own SSS radius (`radius(0.98f, 0.98f, 0.98f)` blocks) is small relative to the
  cloud mesh's thickness (multiple blocks per `CloudMeshGenerator`), so even the local SSS glow
  doesn't make the cloud layer look thin or translucent -- it mostly just lights the top/near
  surface while the far side (what's visible from below, or when backlit) stays dark.

**This is not a Linux porting bug.** It's how NVIDIA's Remix material system and this mod's
choice of material type (opaque+SSS, tuned presumably for Windows/DLSS-RR denoising) combine:
clouds are geometrically real, opaque-family objects in the path tracer, so they cast real
shadows and block the sun largely the way any other opaque geometry would, with only a thin
near-surface SSS glow as decoration. Changing this would mean either reworking the cloud
material to the translucent family (a real transmission BSDF) or retuning the SSS parameters
(larger radius, different albedo/transmittance) -- a rendering-architecture change, not a small
or clear bug fix, and out of scope to attempt blind in this session without Windows reference
screenshots or an NVIDIA engineer's intended look to compare against.

## Decision: safe mitigation, not a code fix

Per the task brief, documented the cause and chose the suggested safe mitigation instead of
touching the material/shader code: `scripts/install.sh` now writes `renderClouds:"false"` into
the instance's `options.txt` on install, **only if the file doesn't already set
`renderClouds`** (so an existing choice -- including a user turning clouds back on -- is never
overwritten). This is reversible any time from Video Settings in-game.

The ~131 draws / ~1.3M triangles per frame performance angle (also mentioned in the task
brief) is moot with clouds off, though not separately measured this session since the visual
cause above was the more direct/severe problem.

## Files

- `evidence/look-and-run/job2_clouds_on_sunlit.png`, `job2_clouds_off_sunlit.png` -- before/after
  at the sunlit view, noon, clear weather.
- `evidence/look-and-run/job2_clouds_on_shadows.png` -- ground-level view showing the broader
  harsh-shadow look with clouds on at a low sun angle.
- `scripts/install.sh` -- writes `renderClouds:"false"` to a fresh/existing `options.txt` that
  doesn't already set it.
