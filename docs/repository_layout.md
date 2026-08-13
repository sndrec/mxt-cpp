# Repository and local-asset layout

The tracked repository uses these top-level homes:

- `mxto/`: the Godot project, scenes, scripts, runtime resources, and required
  vendored add-on binaries
- `src/`: first-party native production source, grouped by runtime domain
- `tools/`: standalone authoring, conversion, and inspection tools
- `scripts/`: repository build, smoke, packaging, and maintenance entry points
- `services/`: separately deployed service programs
- `reference/`: tracked reference implementations and snapshots that are not
  production build inputs
- `track-editor-blender-plugin/`: the Blender add-on package and authoring
  template
- `track_source_files/`: tracked source `.blend` files for official tracks
- `docs/`: maintained formats, workflows, and repository guidance

Generated or machine-local material belongs outside production source:

- `tmp/`: disposable builds, test captures, conversion intermediates, logs,
  probes, and local investigations
- `mxto/bin/`: descriptors and required vendored runtime libraries; first-party
  `libgamesim` outputs are rebuilt locally and are not tracked
- local exports and decoded/import dumps: keep beneath an ignored local work
  directory, not beside source
- marketing exports and source art: keep in a deliberate asset workspace until
  the project selects canonical tracked deliverables and their final home

Untracked files are never moved or deleted as part of repository cleanup without
their owner's explicit confirmation.
