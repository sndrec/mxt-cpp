# Build and verification

This file describes the repository-wide verification entry points. During the
active repository refactor, follow the cadence in
`REPOSITORY_REFACTOR_PLAN.md`.

## During implementation

Do not run test suites between structural slices. For each code or scene
checkpoint, use only the required release compile and a bounded game launch.

```powershell
scons target=template_release -j4
& 'A:\programs\Godot_v4.7.1-stable_win64\Godot_v4.7.1-stable_win64_console.exe' --path 'B:\programming\mxt-cpp\mxto' --quit-after 120
```

The launch is successful when the project opens and exits without a native
library load failure, GDScript parse error, invalid scene/resource error, or
startup crash.

The console executable launches the same editor/runtime as the GUI executable,
but keeps automated invocations attached so their output and exit status can be
inspected. A forced timed exit can report render-thread finalization warnings;
those are shutdown diagnostics rather than evidence that the project failed to
open.

Never pass `debug_symbols` and do not substitute a debug build.

## Deferred test suite

`scripts/run_smoke_suite.ps1` is the composable test entry point for the final
verification milestone. It is intentionally not run during structural
implementation.

List its groups without running tests:

```powershell
& .\scripts\run_smoke_suite.ps1 -Group list
```

Run the single-process scene, UI, content, and controller group:

```powershell
& .\scripts\run_smoke_suite.ps1 -Group stable
```

Run deterministic input, bumper, and netstate checks:

```powershell
& .\scripts\run_smoke_suite.ps1 -Group simulation
```

Run all groups whose local prerequisites are available:

```powershell
& .\scripts\run_smoke_suite.ps1 -Group all-applicable
```

The replay group requires a compatible replay:

```powershell
& .\scripts\run_smoke_suite.ps1 -Group replay -ReplayPath 'B:\path\to\capture.mxtreplay'
```

The lobby-load group invokes `scripts/run_lobby_load_test.ps1` and accepts an
explicit tracks directory:

```powershell
& .\scripts\run_smoke_suite.ps1 -Group lobby-load -LobbyClients 40 -TracksDirectory 'B:\path\to\track'
```

The runner writes one log per child invocation beneath a timestamped directory
in the system temporary directory by default. Use `-OutputDirectory` to retain
logs elsewhere.

## Runner exit codes

- `0`: every requested, applicable test passed.
- `1`: one or more test children failed.
- `2`: a required executable, file, argument, or other prerequisite was absent.

The runner contains a child-failure propagation check. Execute it during final
verification, not during implementation:

```powershell
& .\scripts\run_smoke_suite.ps1 -Group self-check
```
