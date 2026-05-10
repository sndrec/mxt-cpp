# F-ZERO GX Track Conversion Prototype

This is a two-stage prototype for loosely converting F-ZERO GX `COLI_COURSE`
analytic road data into MXT track-editor files.

1. `gx_track_sampler.cpp` uses the decomp's `fzgx_content` loader/evaluator to
   sample the primary analytic driveable road surface.
2. `gx_samples_to_mxt_blend.py` runs inside Blender, creates MXT road-segment
   empties with baked curve-matrix fcurves, saves a `.blend`, and calls the
   existing MXT plugin exporter to write `.mxt_track`, `.obj`, and `.json`.

Example:

```powershell
.\scripts\fzgx\convert_gx_track.ps1 `
  -CoursePath A:\programs\smb1-decomp\src-fzgx\fzgx-iso\files\stage\COLI_COURSE01.lz
```

The default output folder is:

```text
A:\blender_stuff\mxt\mxt_track_editor\tracks\fzgx_imports
```

Current limitations:

- The sampler follows the primary branch/driveable surface only.
- Decorative/collision triangle mesh data is intentionally ignored.
- Shape-family support is best-effort; Course 01 currently converts as flat
  road throughout.
- Trigger objects, embeds, materials, and GX-specific effects are not ported yet.
