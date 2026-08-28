# F-ZERO GX Track Conversion Prototype

This is a two-stage prototype for loosely converting F-ZERO GX `COLI_COURSE`
analytic road data into MXT track-editor files.

1. `gx_track_sampler.cpp` uses the decomp's `fzgx_content` loader/evaluator to
   sample the primary analytic driveable road surface.
2. `gx_samples_to_mxt_blend.py` runs inside Blender, creates MXT road-segment
   empties with baked curve-matrix fcurves, saves a `.blend`, and calls the
   existing MXT plugin exporter to write `.mxt_track`, `.glb`, and `.json`.

Example:

```powershell
.\tools\track\fzgx\convert_gx_track.ps1 `
  -CoursePath A:\programs\smb1-decomp\src-fzgx\fzgx-iso\files\stage\COLI_COURSE01.lz
```

The default output folder is:

```text
A:\blender_stuff\mxt\mxt_track_editor\tracks\fzgx_imports
```

## Current conversion coverage

- The sampler follows the primary branch and its analytic driveable surface.
- Conversion output contains MXT road segments and the preview/collision exports
  produced from those segments.
- Shape-family mapping is best-effort; Course 01 currently uses flat road
  segments throughout.
