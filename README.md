# MaxX Throttle

MaxX Throttle is a Godot racing game backed by a native C++ simulation. This
repository also contains content tooling, a Blender track-editor add-on, and the
leaderboard verification service.

## Repository map

- `mxto/` — Godot project, scenes, GDScript runtime, content, and UI
- `src/` — native GDExtension source, organized by domain
- `tools/` — standalone track conversion and inspection tools
- `scripts/` — build, smoke, packaging, and maintenance entry points
- `track-editor-blender-plugin/` — modular Blender track-editor add-on
- `services/leaderboard_verifier/` — separately deployed leaderboard service
- `reference/` — non-production reference implementations and snapshots
- `track_source_files/` — tracked source `.blend` files for official tracks

## Runtime ownership

The Godot scene owns game meaning and presentation. `mxto/main.gd` coordinates
top-level navigation; focused scene-owned controllers handle race lifecycle,
debug launch behavior, content selection, presentation, spectators, lobby UI,
and communication.

Networking is coordinated by `mxto/netplay/network_manager.gd`, with direct
owners for lobby settings, race admission, input transport/timing/rollback,
state transfer, results, and telemetry. RPCs live on the node that owns their
state; `NetworkManager` is lifecycle coordination, not a forwarding facade.

Native production source is grouped under `src/gamesim`, `src/car`, `src/track`,
`src/core`, `src/netcode`, `src/audio`, `src/render`, `src/camera`,
`src/content`, and `src/platform/steam`. Generated auth-input dictionaries live
under `src/netcode/generated`; F-Zero reference code is outside the production
tree under `reference/fzgx`.

## Prerequisites

- Godot 4.7.1; the configured Windows editor is
  `A:\programs\Godot_v4.7.1-stable_win64\Godot_v4.7.1-stable_win64.exe`
- Python and SCons available on `PATH`
- Visual Studio C++ Build Tools on Windows
- the `godot-cpp` submodule on its required `4.4` branch
- optional Steamworks SDK through `STEAMWORKS_SDK_ROOT`, `steamworks_sdk=...`,
  or a sibling `../steamworks-sdk` directory

Initialize submodules before the first build. Vendored zstd and Opus sources are
compiled by SCons; no separate library install is required.

## Build and launch

Build the required release target from the repository root:

```powershell
scons target=template_release -j4
```

SCons writes intermediate objects under `tmp/build/objects` and produces the
first-party runtime library under `mxto/bin`. Open the project with:

```powershell
& 'A:\programs\Godot_v4.7.1-stable_win64\Godot_v4.7.1-stable_win64.exe' --path "$PWD\mxto"
```

## Tests

List the available smoke groups:

```powershell
.\scripts\run_smoke_suite.ps1 -Group list
```

The stable single-process suite and deterministic simulation suite are:

```powershell
.\scripts\run_smoke_suite.ps1 -Group stable
.\scripts\run_smoke_suite.ps1 -Group simulation
```

Replay and lobby-load groups require their documented inputs/helpers. Smoke
logs default to a timestamped system temporary directory rather than the repo.
The Blender add-on's lightweight Python tests are:

```powershell
python -m unittest discover track-editor-blender-plugin/tests
```

## Tool and service entry points

- Blender add-on setup: [track-editor-blender-plugin/README.md](track-editor-blender-plugin/README.md)
- official vehicle reference repair: [tools/car/README.md](tools/car/README.md)
- F-Zero track conversion: [tools/track/fzgx/README.md](tools/track/fzgx/README.md)
- auth-input dictionary training: `scripts/train_auth_input_dict.py --help`
- playtester export packaging: `scripts/build_playtester_exports.ps1`
- leaderboard service: [services/leaderboard_verifier/README.md](services/leaderboard_verifier/README.md)
