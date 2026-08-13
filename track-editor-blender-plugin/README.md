# MXT Blender track editor

`mxt_track_editor` is the Blender add-on package. Install or link that directory
into Blender's add-ons directory, then enable **MXT Racetrack Road Creator**.
The adjacent `track_editor_base.blend` file is the authoring template.

The package is organized by responsibility:

- `foundation.py`: properties, segment editing, and live-update scheduling
- `shapes.py` and `curve_matrix.py`: road geometry and curve sampling
- `mesh.py`: rendered mesh, checkpoints, and collision data
- `nav_*.py`: CPU navigation analysis and graph generation
- `export.py`: `.mxt_track` serialization and export operators
- `ui.py`: the Blender sidebar

Run the non-Blender helper checks from the repository root with:

```powershell
python -m unittest discover track-editor-blender-plugin/tests
```

An actual add-on enable/import/export smoke still requires Blender 4.x.
