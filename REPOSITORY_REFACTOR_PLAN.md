# MaxX Throttle repository-scale refactor plan

## Status

- Authoritative plan: this file.
- The older `REFACTOR_PLAN.md` is not an input to this effort and must be ignored.
- Implementation is active. Milestones 0 through 6 are complete; Milestone 7
  is in progress.
- Existing commit history must be preserved. Do not squash, rebase, filter, or
  otherwise rewrite the 64 commits currently ahead of `origin/before-cpu-driver`.
- Repository-artifact cleanup is intentionally scheduled after the architectural
  refactors, except where a generated file would otherwise be accidentally staged.

## Objective

Refactor the live MaxX Throttle repository into explicit, cohesive ownership
boundaries without changing game behavior, network compatibility, deterministic
simulation results, content formats, visual output, or hot-path performance.

The finished repository should be easier to navigate and modify because:

- `GameManager` coordinates game states instead of implementing unrelated
  lobby, chat, content, presentation, debugging, and race subsystems;
- `NetworkManager` coordinates network lifecycle while protocol mechanics,
  admission, synchronization, transfer, and telemetry have clear owners;
- native source files live under domain directories with direct includes and
  build entries;
- standalone tools are packages of cohesive modules instead of single giant
  scripts;
- generated output, local scratch data, reference material, distributable
  assets, and production source are visibly distinct;
- every completed slice has an independent commit, while expensive validation
  is consolidated into the final verification milestone.

This is a structural refactor. New product features and intentional gameplay
changes are outside scope.

## Current live-tree observations

These are starting observations, not permanent assumptions. Reinspect the live
tree before each phase.

- Active branch: `before-cpu-driver`.
- The branch is 64 commits ahead of `origin/before-cpu-driver`; preserve them.
- The worktree contains user-owned source changes, generated build churn, local
  marketing artwork, and large local track directories. Do not destroy or fold
  unrelated work into refactor commits.
- `mxto/main.gd` is roughly 4,900 physical lines and owns many unrelated domains.
- `mxto/netplay/network_manager.gd` is roughly 3,700 lines and mixes session,
  lobby, transport, synchronization, state transfer, result, and telemetry work.
- `src/mxt_core/netcode_session.cpp` is roughly 3,700 lines even after some
  implementation was moved into adjacent files.
- `track-editor-blender-plugin/mxt_track_editor.py` is roughly 8,200 lines.
- `src/car/physics_car.cpp` is large, but it is a performance-sensitive hot path
  and must not be split merely to satisfy a line-count target.
- `GameSim` implementations are already divided among several `gamesim_*.cpp`
  files. Preserve useful existing boundaries.
- The repository tracks first-party build products such as `.obj`, `.pdb`, and
  game-extension binaries. Their cleanup belongs to the later hygiene phase.
- The current required native verification build is:

  ```powershell
  scons target=template_release -j4
  ```

- Do not pass `debug_symbols` and do not substitute a debug target.
- The configured Godot executable is:

  ```text
  A:\programs\Godot_v4.7.1-stable_win64\Godot_v4.7.1-stable_win64.exe
  ```

## Non-negotiable constraints

### Git and user work

- Preserve all existing commits and their order.
- Never rewrite published or unpublished history during this plan.
- Before every slice, inspect `git status --short --branch`.
- Treat all pre-existing modified and untracked files as user-owned.
- Never delete, revert, overwrite, stage, or commit unrelated user work.
- Do not use `git reset --hard`, `git checkout --`, or broad clean commands.
- Do not use a refactor commit to hide unrelated generated or binary churn.
- Stage exact paths only and inspect the complete staged diff before committing.
- Make one commit per coherent, verified slice. Do not accumulate the whole plan
  into one commit.
- Record every refactor commit hash in the progress log at the bottom of this
  file.

### Architecture

- No wrappers, forwarding methods, compatibility aliases, shims, or adapters.
- When ownership moves, update every caller to the new owner in the same slice.
- Remove replaced state, signals, methods, constants, and dead code immediately.
- Do not preserve deprecated paths “just in case.”
- Do not create components merely to reduce line counts. Each new owner must
  have cohesive state, lifecycle, and behavior.
- Prefer typed direct references over `has_method()`, string-based `call()`,
  parent traversal, or implicit scene-tree discovery.
- Keep scene-owned Godot behavior on scene-owned nodes. Prefer static `.tscn`
  structure for stable UI and node composition.
- Keep mission or product meaning out of generic native APIs.
- Do not change replay schemas, content formats, network protocol behavior,
  authority rules, RPC reliability/channel choices, or save compatibility as
  part of structural moves.

### Performance and determinism

- The native simulation remains deterministic at 60 Hz.
- No new per-tick or per-frame allocation in native simulation, rendering,
  audio, or packet hot paths.
- No new virtual dispatch, redundant state copies, cache-hostile ownership, or
  cross-language calls in hot loops.
- Preserve fixed pools, scratch storage, SoA data, and direct calls.
- Do not move performance-sensitive native work into GDScript.
- Do not split `PhysicsCar`, `GameSim`, or `NetcodeSession` hot paths until their
  call graph, data ownership, and verification coverage are understood.

### Scope and behavior

- Structural equivalence is the default. If a bug is discovered, document it
  and fix it in a separate commit only when the fix is necessary to complete
  the refactor safely.
- Do not redesign UI, controls, physics, game rules, networking policy, content
  identity, Steam behavior, or authoring formats.
- Do not rename public content identifiers or serialized dictionary keys merely
  for aesthetics.
- Keep vendor code and generated reference data out of style-only rewrites.

## Working protocol for every milestone

1. Read this entire plan and the live `AGENTS.MD`.
2. Inspect the worktree and identify pre-existing changes that overlap the
   intended slice.
3. Read the complete source and all direct callers for the ownership boundary.
4. Write down the exact old owner, new owner, state being moved, lifecycle
   hooks, RPC paths, and verification commands in the progress log.
5. Add or strengthen a focused smoke/contract test before or with the move when
   current coverage cannot prove equivalence, but do not run test suites during
   implementation.
6. Move the implementation and update all callers directly in one cutover.
7. Search for the old symbols and remove obsolete paths.
8. For every code or scene checkpoint, run the required release compile and
   launch the game far enough to confirm that the project opens without native
   loading, parse, or startup errors. Defer all test suites to Milestone 11.
9. Run `git diff --check`, inspect `git diff`, and stage exact paths only.
10. Commit the completed structural slice with a specific imperative subject;
    the commit may precede full runtime validation under the cadence below.
11. Update the checkboxes and progress log in this file in the same commit or a
    directly adjacent plan-status commit.
12. Proceed to the next slice when its direct migration is complete, old symbols
    are gone, the diff has been reviewed, the release compile succeeds, and the
    game opens. Do not run the deferred test suites.

If unrelated pre-existing edits overlap a required file, preserve their exact
intent. If that intent cannot be established from the diff and surrounding
code, stop that slice and move to a non-overlapping milestone or request user
direction. Never guess by reverting or absorbing the edit.

## Verification framework

### User-directed verification cadence

- Complete the architectural and repository work first, then run the complete
  test matrix and repair failures at the end.
- During Milestones 0–10, verification for every code or scene checkpoint is
  limited to the required release compile and launching the game to confirm it
  opens successfully.
- Do not run unit, smoke, integration, replay, netplay, content, tool, Blender,
  export, or other test suites during implementation.
- Symbol searches, diff inspection, and `git diff --check` remain part of editing
  discipline; they are not substitutes for the deferred tests.
- Continue making coherent commits during the refactor. Record tests as
  `deferred to Milestone 11` rather than implying they passed.
- Milestone 11 is a dedicated build/test/fix loop: run the full applicable
  matrix once after all planned work is assembled, diagnose every failure, make
  focused repair commits, and rerun only the failing groups until they pass.
- After targeted repairs pass, run one final full applicable matrix to establish
  the completion evidence.

### Required implementation checkpoint

- Compile with exactly:

  ```powershell
  scons target=template_release -j4
  ```

- Launch the game project with the configured Godot executable and a bounded
  automatic exit, for example:

  ```powershell
  & 'A:\programs\Godot_v4.7.1-stable_win64\Godot_v4.7.1-stable_win64_console.exe' --path 'B:\programming\mxt-cpp\mxto' --quit-after 120
  ```

- Confirm that the game reaches startup and exits without a native-library load
  failure, GDScript parse error, invalid scene/resource error, or startup crash.
- Do not invoke any `mxto/test` script or other test suite at this checkpoint.
- Run `git diff --check`.

### Native checkpoint rules

- Always use the same required release command during implementation:

  ```powershell
  scons target=template_release -j4
  ```

- After it succeeds, perform the same bounded game launch described above.
- Defer focused Godot binding tests, packet roundtrip, and state restore
  equivalence tests to Milestone 11.
- Inspect whether the build changed tracked artifacts and keep that churn out of
  the source commit until the repository-hygiene phase establishes the final
  policy.
- Run `git diff --check`.

### Required final smoke coverage

Use the existing tests when applicable and create one composable PowerShell
runner during Milestone 1. Do not run the suite until Milestone 11. The stable
suite should cover at least:

- main-scene headless load;
- replay controller;
- race audio controller;
- track content controller;
- text-chat history and rate limiting;
- lobby chibi rendering and lifecycle;
- lobby scale and empty-track behavior;
- netplay admission, input resilience, and state transfer;
- authoritative input packet roundtrip;
- netstate restore and native-range equivalence;
- bumper netstate;
- custom-stamp networking;
- content package validation;
- car-property sampling and codec roundtrip;
- vehicle restore/elimination;
- voice capture dynamics;
- Grand Prix grid/results behavior.

Tests that require an external replay, multiple processes, Steam, exported track
content, or another machine must be explicit optional groups with documented
arguments. Never silently report them as passing when prerequisites were absent.

## Target ownership map

Names below describe ownership, not mandatory filenames. Confirm names against
the live code before creating files.

### Godot runtime

- `GameManager`: top-level application/race-state orchestration only.
- `LobbyController`: lobby screen state, roster presentation, race-option UI,
  and lobby transitions.
- `LobbyChibiController`: chibi lifecycle, rendering, hover/magnifier behavior,
  compact state serialization, and its RPC endpoints.
- `ChatController`: lobby/race chat submission, validation, rate limiting,
  history, rendering handoff, and chat RPC endpoints.
- `VehicleContentController`: official/local/Workshop vehicle discovery,
  definitions, materials, meshes, content evidence, and test-drive content
  lookup.
- `RacePresentationController`: results overlay, notifications, medals,
  nametags, placement badges, and presentation-only timers.
- `SpectatorController`: elimination spectator and live-finished-spectator input
  and camera focus.
- `RaceSessionController`: race creation/teardown and transition coordination
  that is not native simulation or network transport.
- `DebugRuntimeController`: screenshots, render profiling, CLI-only profiling
  flags, and diagnostic labels that should not burden normal orchestration.
- Existing `ReplayController`, `RaceAudioController`, `TrackContentController`,
  `CustomStampNetworkController`, and `RaceCommunicationOverlay` remain direct
  owners unless inspection proves a boundary is wrong.

Do not force every listed name into a separate node if live state demonstrates
that two responsibilities share one lifecycle and data owner. Conversely, do
not leave an implementation in `GameManager` solely because it calls several
subsystems.

### Native runtime

- `src/core`: small general-purpose deterministic math, memory, debugging, and
  curve utilities only.
- `src/gamesim`: `GameSim`, its internal headers, rendering submission,
  instantiation, state serialization, CPU input, bumpers, and race effects.
- `src/car`: car data and physics implementation.
- `src/track`: runtime track geometry and collision.
- `src/content`: archive, manifest, catalog, package, and validation code.
- `src/netcode`: `NetcodeSession`, packet/state-transfer code, compression
  dictionaries, and bindings.
- `src/audio`: Opus and pooled spatial-audio implementation.
- `src/platform/steam`: Steam service implementation.
- `src/render`: reusable native rendering/mesh builders that are not owned by
  `GameSim`.
- `src/camera`: gameplay camera implementations if they are independent of
  `GameSim`; otherwise keep them inside `src/gamesim`.
- `reference/fzgx`: non-production F-Zero GX reference/port material currently
  nested beneath `src`.

This directory map is a target to validate, not permission for one giant move.
Move one domain at a time, update includes and `SConstruct` directly, compile,
launch the game, and commit. Defer tests to Milestone 11.

### Tools and services

- `tools/car`: car-property codecs, editors, graphing, and their tests.
- `tools/track`: track conversion/sampling utilities.
- `tools/blender/mxt_track_editor`: the Blender add-on as a real Python package
  with cohesive modules.
- `services/leaderboard_verifier`: keep the deployable service self-contained.
- `reference`: historical or reverse-engineering inputs not compiled into the
  game.
- `local` or another clearly ignored workspace root: user-local captures,
  exports, scratch decodes, logs, and large working assets. Do not move material
  user files there without explicit confirmation during the hygiene milestone.

## Milestones

### Milestone 0 — Establish the protected baseline

- [x] Re-read `AGENTS.MD` and this plan.
- [x] Record branch, HEAD, upstream, ahead/behind count, and full short status.
- [x] Confirm the 64 existing commits will not be rewritten.
- [x] Classify current changes as meaningful source, generated build output,
      local assets, scratch/reference data, or unknown.
- [x] Do not commit, discard, or move ambiguous user-owned files.
- [x] Identify which current edits overlap planned phases.
- [x] Record the exact verification commands that work in the live checkout.

Completion condition: the executor can name every pre-existing dirty category,
knows which paths are protected, and can begin a non-overlapping refactor slice
without rewriting history or losing work.

### Milestone 1 — Make verification repeatable

- [x] Add a PowerShell smoke runner with named test groups and explicit exit
      codes.
- [x] Default it to the Godot executable required by `AGENTS.MD` while allowing
      an explicit override.
- [x] Keep single-process stable tests separate from replay-, Steam-, export-,
      and multi-process tests.
- [x] Add a concise verification document or README section explaining the
      release build and smoke groups.
- [x] Add a failure-propagation self-check to the runner; execute it only in
      Milestone 11.
- [x] Run the baseline release compile and bounded game launch.
- [x] Do not run the stable smoke group before structural edits; schedule every
      test group for Milestone 11.

Completion condition: later milestones have a single reliable command for the
stable Godot suite plus the exact required native build command. The baseline
release compile and game launch succeed; the test runner remains deferred until
Milestone 11.

### Milestone 2 — Extract lobby chibi ownership

- [x] Create a scene-owned, typed `LobbyChibiController`.
- [x] Move chibi constants, state, renderer setup/teardown, roster lifecycle,
      hover, magnifier, packing, batching, and RPC endpoints out of
      `GameManager`.
- [x] Make `LobbyChibiCar` refer directly to its typed controller and explicit
      data providers.
- [x] Remove its `game_manager` pseudo-interface and all associated
      `has_method()`/string `call()` dispatch.
- [x] Update every input, process, network, and teardown call site directly.
- [x] Update the lobby-chibi smoke test to assert the new owner and absence of
      the old methods on `GameManager`.

Completion condition: `GameManager` contains no lobby-chibi implementation or
state, RPC paths resolve on the new node, the release compile and game launch
succeed, and the lobby-chibi/lobby-scale tests are recorded for Milestone 11.

### Milestone 3 — Extract chat and communication ownership

- [x] Create a typed chat owner for lobby and in-race text communication.
- [x] Move sanitization, per-sender/global rate limiting, bounded history,
      submission, lobby rendering, race-overlay handoff, focus behavior, and RPC
      endpoints out of `GameManager`.
- [x] Keep voice capture/playback in its existing voice owner; expose only the
      minimal typed status needed by the communication overlay.
- [x] Replace dynamic `get_node_or_null()`/`has_method()` voice-status polling
      with a typed direct boundary where practical.
- [x] Update all RPC paths directly without compatibility aliases.
- [x] Strengthen the existing chat smoke test around ownership, bounded memory,
      channels, and lifecycle reset.

Completion condition: `GameManager` owns no chat history, rate limiter, chat RPC,
or lobby-chat rendering implementation; the release compile and game launch
succeed, and chat plus voice-adjacent tests are recorded for Milestone 11.

### Milestone 4 — Extract vehicle content ownership

- [x] Create or consolidate a typed vehicle-content owner.
- [x] Move official, local package, test-drive snapshot, and Workshop vehicle
      discovery out of `GameManager`.
- [x] Move car-definition indexing, content evidence, packaged mesh/material
      construction, texture resolution, and vehicle metadata transforms.
- [x] Give UI, lobby chibis, car rendering, test drive, and race setup direct
      typed access to the vehicle-content owner.
- [x] Keep generic package parsing/validation in the existing native content
      APIs.
- [x] Remove duplicate content lookup and material-building paths.
- [x] Add focused content/vehicle smoke coverage where existing package tests do
      not prove runtime definition and render lookup.

Completion condition: `GameManager` does not scan or construct vehicle content;
the release compile and game launch succeed, and official/local/Workshop vehicle
lookup plus content tests are recorded for Milestone 11.

### Milestone 5 — Separate lobby, race presentation, and spectator behavior

Complete this milestone as multiple commits, one owner at a time.

- [x] Move lobby option controls, stage selection/preview, roster presentation,
      CPU buttons, and lobby-only transition presentation into a cohesive lobby
      owner.
- [x] Prefer static `.tscn` nodes for stable controls that are currently built
      dynamically, preserving exact appearance and behavior.
- [x] Move race-results overlay, result formatting, machine-setting selection,
      notifications, medals, nametags, and placement badges into a race
      presentation owner.
- [x] Move elimination and finished-racer spectator selection/input into a
      spectator owner.
- [x] Keep race rules and authoritative results in their existing simulation or
      network owners; presentation reads those results without redefining them.
- [x] Remove old `GameManager` state and direct node-field sprawl after each
      cutover.

Completion condition: each owner has an explicit lifecycle, the release compile
and game launch succeed, affected UI and Grand Prix/vehicle-restore tests are
recorded for Milestone 11, and `GameManager` is visibly an orchestrator rather
than a presentation implementation.

### Milestone 6 — Separate race lifecycle and debug runtime work

- [x] Isolate race-world setup/teardown and menu/lobby/race transitions in a
      cohesive race-session owner while keeping native simulation calls direct.
- [x] Preserve replay, audio, track, network, and content lifecycle ordering.
- [x] Move clean 4K screenshot behavior, render-profile counters/reporting,
      diagnostic CLI modes, and debug-label visibility into a debug runtime
      owner.
- [x] Ensure debug owners do not allocate or process during normal gameplay when
      disabled.
- [x] Consolidate duplicated transition cleanup without creating a forwarding
      facade.
- [x] Add transition smoke coverage for menu, lobby, single-player, multiplayer,
      replay, and test-drive exits where feasible.

Completion condition: `GameManager` retains top-level state coordination and the
fixed tick/process entry points, but no longer contains large presentation,
debugging, content, chat, or lobby-chibi implementations. Target approximately
2,000 lines or fewer only if the remaining responsibilities are genuinely
cohesive.

### Milestone 7 — Decompose `NetworkManager` by protocol ownership

This is the highest-risk GDScript phase. Do not start until Milestones 1–6 are
stable and the network smoke matrix is reliable.

- [x] Map every RPC, authority rule, transfer mode, channel, sender validation,
      and call site before editing.
- [x] Separate connection/version/peer lifecycle from lobby roster/settings.
- [x] Separate race admission and synchronized-start state.
- [ ] Separate authoritative input transport, timing, acknowledgement, state
      transfer/FEC, rollback coordination, results/events, and telemetry where
      their state and lifecycles are distinct.
- [ ] Keep RPC methods on their real owning nodes and migrate every caller/path
      in one cutover per slice.
- [ ] Move packet/history work into native `NetcodeSession` only when that
      reduces GDScript hot-path work without adding cross-language chatter or
      allocations.
- [ ] Keep diagnostic aggregation outside protocol-critical loops when possible.
- [ ] Remove duplicated dictionaries/state after native ownership is proven.
- [ ] Record affected single-process and multi-process netplay groups after each
      protocol slice, but do not run them until Milestone 11.

Completion condition: network responsibilities have explicit owners,
`NetworkManager` is a lifecycle coordinator rather than a monolith, all RPC
paths and channels are deliberate, the release compile and game launch succeed,
and the complete admission/input/state-transfer/rollback matrix is queued for
Milestone 11.

### Milestone 8 — Organize native source domains

Perform literal source moves one domain at a time.

- [ ] Move `GameSim` sources and internal headers into `src/gamesim`.
- [ ] Rename generic `src/main.cpp`/`src/main.h` to names that identify
      `GameSim`, updating all includes and build paths directly.
- [ ] Split the current mixed `src/mxt_core` directory into validated `core`,
      `netcode`, `audio`, `platform/steam`, and `render` domains.
- [ ] Move generated netcode dictionaries beneath their real netcode owner and
      document how they are produced.
- [ ] Decide camera ownership from the live dependency graph and place it under
      `camera` or `gamesim`.
- [ ] Move `src/fzgx-reference` out of production `src` into `reference/fzgx`,
      updating every real script/tool reference.
- [ ] Replace broad source globs with explicit domain globs or lists that make
      production membership obvious.
- [ ] Reassess large native implementation files by data ownership and hot-path
      constraints; split only where direct implementation-unit boundaries exist.
- [ ] Keep headers narrow and remove stale includes after each move.

Completion condition: the native tree communicates domain ownership at a glance,
reference code is outside production source, direct includes and build entries
are updated, the release compile and game launch succeed, and no include wrapper
or compatibility header remains.

### Milestone 9 — Modularize standalone tools

Do not touch a tool while overlapping pre-existing user edits are unresolved.

- [ ] Turn `track-editor-blender-plugin/mxt_track_editor.py` into a package with
      modules for data/format handling, geometry, import/export operators,
      validation, UI panels, and Blender registration.
- [ ] Keep Blender-required registration entry points thin and direct; do not
      preserve the old monolith as a forwarding shell.
- [ ] Organize car-property codec/editor/graph tooling under `tools/car` with one
      authoritative format implementation and focused tests.
- [ ] Organize F-Zero/track sampling and conversion utilities under
      `tools/track`.
- [ ] Update imports and documented invocations directly.
- [ ] Add non-Blender unit tests for pure format/geometry helpers and perform an
      actual Blender add-on enable/import/export smoke when Blender is available.

Completion condition: no hand-written tool source file exceeds 4,000 lines,
modules normally stay below 2,000 lines, format logic has one owner, and existing
tool workflows remain directly executable.

### Milestone 10 — Repository and generated-artifact hygiene

This milestone cleans the current tree without rewriting history.

- [ ] Inventory every tracked `.obj`, `.pdb`, DLL, temporary replacement file,
      log, generated database, archive, and decoded scratch asset.
- [ ] Distinguish reproducible first-party build outputs from required vendored
      runtime binaries. Do not blanket-ignore or remove all DLLs.
- [ ] Prove first-party outputs can be regenerated from a fresh checkout before
      removing them from the index.
- [ ] Update `.gitignore` for first-party objects, PDBs, SCons outputs, Godot
      replacement files, logs, Python caches, and local scratch roots.
- [ ] Remove reproducible generated outputs from tracking in an additive cleanup
      commit; do not purge old history.
- [ ] Establish a documented top-level home for marketing assets, source art,
      track source files, local exports, references, and temporary investigations.
- [ ] Do not delete or move untracked user assets without explicit confirmation.
      When confirmation is unavailable, preserve them and ignore them locally.
- [ ] Remove empty, obsolete, or duplicate tracked directories only after their
      contents and references are proven unnecessary.
- [ ] Ensure validation builds no longer dirty the source tree except for the
      intentionally produced runtime library if that remains the documented
      policy.

Completion condition: a normal release build and smoke run do not scatter
generated files through source directories; required vendor binaries remain
available; no user asset was lost; and the cleanup is represented by ordinary
forward commits.

### Milestone 11 — Documentation, final testing, and repair loop

- [ ] Replace the nearly empty README with a concise repository map, prerequisites,
      release build, test commands, tool entry points, service entry points, and
      generated/local asset policy.
- [ ] Document each runtime owner and its lifecycle at the level needed to find
      code, not as redundant implementation prose.
- [ ] Remove stale planning documents or mark them superseded without deleting
      user history.
- [ ] Finish all planned structural, tooling, and hygiene work before starting
      the expensive validation sequence.
- [ ] Run the full stable smoke suite once.
- [ ] Run applicable optional replay, multi-process, Steam, Blender, and export
      groups when prerequisites exist; record skipped prerequisites honestly.
- [ ] Run the required release build from a clean source state.
- [ ] Run Python/tool tests.
- [ ] Diagnose every failure and make focused repair commits without backing out
      valid architectural ownership.
- [ ] Rerun only failing groups while repairing them.
- [ ] After repairs pass, run one final full applicable validation matrix.
- [ ] Run `git diff --check` and inspect final status.
- [ ] Confirm there are no goal-owned uncommitted changes.
- [ ] Confirm pre-existing unrelated files remain intact.
- [ ] Add the final commit/test summary to the progress log.

Completion condition: every milestone is checked, every slice is committed,
the final verification matrix passes or has explicitly documented unavailable
external prerequisites, the source ownership map matches reality, and no
required work remains.

## Global definition of done

The repository-scale refactor is complete only when all of the following hold:

- Existing history was preserved and no force push or rewrite occurred.
- All milestone checkboxes are complete.
- `GameManager` and `NetworkManager` are cohesive coordinators, not catch-all
  implementation files.
- Dynamic pseudo-interfaces removed by the plan were replaced with typed direct
  ownership, not wrappers.
- Production native code, reference code, tools, services, generated output,
  and local assets occupy distinct documented locations.
- No hand-written source remains above 4,000 lines without a written,
  performance- or framework-based justification in this plan.
- Normal hand-written source targets 2,000 lines or less where cohesive module
  boundaries exist.
- The release build succeeds with `scons target=template_release -j4`.
- Stable headless smoke tests pass.
- Netcode packet/state equivalence tests pass after relevant changes.
- Builds and tests do not leave goal-owned uncommitted source changes.
- Every implementation slice has a focused commit and recorded evidence.
- No pre-existing user work or local asset was lost.

## Stop rules

Pause the active goal and request user input only when:

- a required move overlaps ambiguous pre-existing user edits whose intent cannot
  be established safely;
- a material untracked asset must be deleted or moved outside the repository;
- an external credential, Steam operation, deployment, or destructive remote
  action is required;
- behavior parity cannot be verified because a required proprietary/external
  input is unavailable;
- the live architecture disproves a required ownership boundary and changing the
  plan would materially alter scope or behavior.

Do not stop merely because a milestone is large, a test initially fails, or the
work spans many turns. Diagnose, repair, verify, commit, update this plan, and
continue.

## Progress log

Append entries; do not rewrite old evidence.

### Plan creation

- Date: 2026-08-13
- State: plan written; implementation not started.
- History policy: preserve the existing commits; cleanup will be additive.
- Initial recommended slice: Milestone 1 verification runner, followed by
  Milestone 2 lobby-chibi ownership.

### Verification-cadence amendment

- Date: 2026-08-13
- User direction: during implementation, stick to the required release compile
  and launching the game to confirm it opens.
- Execution policy: do not run test suites during implementation. Complete the
  refactor first, then run the full applicable test matrix, fix failures, and
  perform one final confirmation run.

### Milestone 0 — protected baseline complete

- Date: 2026-08-13
- Branch: `before-cpu-driver`.
- Baseline HEAD: `06fa94dc22ac77130a033779ab3d60e279ea0de1` (`yippee`).
- Upstream: `origin/before-cpu-driver`.
- Ahead/behind: 64 ahead, 0 behind.
- History rule: preserve all 64 ahead commits in their current order. Commit
  `06fa94dc` includes generated build output and is intentionally not rewritten;
  Milestone 10 will clean the current tree with forward commits.
- Pre-existing modified repository configuration: `.gitignore`, `AGENTS.MD`.
- Pre-existing modified tools: `scripts/mtpoint_graph.py`,
  `scripts/mxt_car_creator.py`.
- Pre-existing untracked tools: `scripts/mxt_car_props_format.py`,
  `scripts/run_lobby_load_test.ps1`, `scripts/test_mxt_car_props_format.py`.
- Pre-existing generated churn: `vc140.pdb`.
- Pre-existing untracked local material: the playtest text capture, 16 root
  marketing/logo PNGs, `stamps/zigo-cutiemark.png`, `track_exports_old/`, and
  `track_ground/`.
- Goal-owned file: `REPOSITORY_REFACTOR_PLAN.md`.
- Overlap policy: leave repository-config edits untouched until Milestone 10;
  leave the existing tool edits and new tool scripts untouched until Milestone
  9; preserve all local material unless the user explicitly authorizes a move.
- SCons resolved to `C:\Users\loser\scoop\shims\scons.ps1`, version 4.5.2.
- Godot resolved to
  `A:\programs\Godot_v4.7.1-stable_win64\Godot_v4.7.1-stable_win64.exe`,
  version `4.7.1.stable.official.a13da4feb`.
- Required implementation compile:
  `scons target=template_release -j4`.
- Required bounded startup launch:
  `A:\programs\Godot_v4.7.1-stable_win64\Godot_v4.7.1-stable_win64.exe --path B:\programming\mxt-cpp\mxto --quit-after 120`.
- No user file was staged, moved, deleted, reverted, or overwritten while
  establishing this baseline.

### Milestone 1 — repeatable verification complete

- Date: 2026-08-13
- Added `scripts/run_smoke_suite.ps1` with stable, simulation, replay,
  lobby-load, all-applicable, and self-check groups plus explicit aggregate exit
  codes and per-child logs.
- Added `VERIFICATION.md` with the required release build, attached bounded game
  launch, deferred groups, prerequisites, and runner exit-code contract.
- Static verification: the PowerShell parser accepted the runner and all 28
  referenced Godot scripts existed. No test group or self-check was executed.
- Release compile: `scons target=template_release -j4` passed in approximately
  17 seconds.
- Startup: the Godot 4.7.1 console companion opened the project with Vulkan and
  exited 0 after approximately 7 seconds. Existing empty-texture diagnostics
  and forced render-thread shutdown/leak warnings were observed; there was no
  native library load failure, script parse/load failure, invalid scene error,
  or startup crash.
- Deferred to Milestone 11: every stable, simulation, replay, lobby-load,
  Steam-dependent, Blender, export, and runner self-check invocation.
- Commit: `5d0703f0` (`Add deferred verification runner`).

### Milestone 2 — lobby chibi ownership complete

- Date: 2026-08-13
- Old owner: `GameManager` in `mxto/main.gd` (UI-node fields, constants,
  render managers, roster/cache state, hover/magnifier behavior, packed state,
  batching, latency display, and RPC endpoints).
- New owner: the scene-owned `LobbyChibiController` node implemented by
  `mxto/ui/lobby_chibi_controller.gd`.
- Direct providers: `GameManager`, `NetworkManager`, `GameSim`, and the lobby
  chat input are passed once during initialization. `LobbyChibiCar` receives its
  controller, `CarDefinition`, and sampled stats directly; it no longer stores a
  `game_manager` pseudo-interface or uses dynamic method dispatch.
- Lifecycle: `GameManager` calls `process_lobby()` from the lobby physics path
  and `clear()` when leaving lobby ownership. The controller owns renderer
  creation, roster creation/removal, hover/magnifier state, and teardown.
- RPC paths: unreliable ordered channel 9 submission and authority batch
  application now resolve on `/root/Main/LobbyChibiController`; the direct state
  endpoint preserves its previous RPC mode on the same owner.
- Callers updated: `GameManager`, `SessionMemoryTelemetry`,
  `lobby_chibi_render_smoke.gd`, and `lobby_scale_smoke.gd`.
- Source result: `mxto/main.gd` fell from roughly 4,900 lines to 4,369 lines;
  the cohesive controller is 453 lines.
- Static checks: no lobby-chibi implementation method remains in `main.gd`, and
  `LobbyChibiCar` contains no `game_manager`, `has_method()`, or string `call()`
  use. `git diff --check` passed.
- Verification: `scons target=template_release -j4` passed; Godot 4.7.1 opened
  the project and exited 0 without native load, script parse/load, scene, or
  startup errors. The known empty-texture and forced render-thread shutdown
  diagnostics remain.
- Deferred to Milestone 11: lobby-chibi render and lobby-scale smoke tests.
- Commit: `787c9627` (`Extract lobby chibi controller`).

### Milestone 3 — communication ownership complete

- Date: 2026-08-13
- Old owner: `GameManager` (lobby widget references, sanitization, sender and
  global rate limits, history/render tracking, focus flow, race-overlay
  lifecycle, input handling, voice polling, and reliable chat RPCs).
- New owner: the scene-owned `CommunicationController` node implemented by
  `mxto/ui/communication_controller.gd`, with the existing
  `RaceCommunicationOverlay` authored as its static scene child.
- Direct providers: `GameManager`, `NetworkManager`, `GameSim`, and
  `ReplayController` are initialized once. The existing scene-owned
  `ProximityVoiceChat` remains responsible for capture/playback and is accessed
  through a typed node reference and direct `get_voice_debug_status()` call.
- RPC paths: reliable channel 8 client submission and authority broadcast now
  resolve on `/root/Main/CommunicationController`; there are no aliases on
  `GameManager`.
- Lifecycle/callers: host/join reset, lobby rendering and focus, race input,
  transition close, per-frame overlay update, and chibi input blocking all call
  the new owner directly.
- Source result: `mxto/main.gd` fell from 4,369 to 4,112 lines; the communication
  controller is 249 lines.
- Static checks: no chat implementation, race communication overlay field, or
  dynamic voice-node lookup remains in `main.gd`; `git diff --check` passed.
- Verification: `scons target=template_release -j4` passed; Godot 4.7.1 opened
  the project and exited 0 without native load, script parse/load, scene, or
  startup errors. The known empty-texture and forced render-thread shutdown
  diagnostics remain.
- Deferred to Milestone 11: text-chat history and voice-adjacent smoke tests.
- Commit: `5fc9727e` (`Extract communication controller`).

### Milestone 4a — vehicle catalog and definition ownership complete

- Date: 2026-08-13
- New owner: the scene-owned `VehicleContentController` in
  `mxto/vehicle/vehicle_content_controller.gd`.
- Moved out of `GameManager`: official/local/test-drive/Workshop discovery,
  definition indexing, package evidence, GLTF mesh construction, material and
  texture resolution, and vehicle metadata transforms.
- Direct consumers: car settings, vehicle and track package editors, Workshop
  browser, lobby chibis, track content, and replay content lookup now retain
  typed references to the owner where their lifecycle allows it.
- Source result: `mxto/main.gd` lost 383 lines in this slice; the cohesive
  vehicle-content owner is approximately 330 lines.
- Static checks: stale `GameManager` discovery/construction helpers are absent
  and `git diff --check` passed.
- Verification: `scons target=template_release -j4` passed; Godot 4.7.1 opened
  the project and exited 0 without native load, script parse/load, scene, or
  startup errors. The known empty-texture and forced render-thread shutdown
  diagnostics remain.
- Deferred to Milestone 11: content/package and runtime vehicle smoke tests.
- Commit: `d32347a7` (`Extract vehicle content catalog`).
- Remaining Milestone 4 work: transfer custom-stamp render payload ownership,
  remove its obsolete `GameManager` paths, and add focused deferred smoke
  coverage before marking the milestone complete.

### Milestone 4 — vehicle content ownership complete

- Date: 2026-08-13
- Render ownership: custom-stamp manifest resolution, local payload fallback,
  atlas-region construction, livery UV rewriting, and render-settings
  normalization now live in `VehicleContentController`.
- Direct render consumers: race setup, lobby chibis, live spectator focus, and
  replay focus use the typed owner. `GameManager` retains only orchestration.
- Dead code removed: the unused race-atlas wrapper, race-only local-payload
  alias, and unused manifest-signature helper were not transferred.
- Focused deferred coverage: added
  `mxto/test/vehicle_content_controller_smoke.gd` to the stable runner. It checks
  owner population, definition identity/render lookup, catalog evidence,
  render-settings normalization, and absence of the old `GameManager` methods.
- Source result: `mxto/main.gd` is now approximately 3,381 lines, down from
  roughly 4,112 at the start of Milestone 4.
- Verification: `scons target=template_release -j4` passed; Godot 4.7.1 opened
  the project and exited 0 without native load, script parse/load, scene, or
  startup errors. The known empty-texture and forced render-thread shutdown
  diagnostics remain.
- Deferred to Milestone 11: the new vehicle-content smoke plus all other
  content/package, lobby, replay, and rendering tests.
- Commits: `d32347a7` (`Extract vehicle content catalog`) and `f61c4bed`
  (`Move vehicle render content ownership`).

### Milestone 5a — lobby ownership complete

- Date: 2026-08-13
- New owner: the scene-owned `LobbyController` in
  `mxto/ui/lobby_controller.gd`.
- Moved out of `GameManager`: Grand Prix stage queue and preview, lobby race
  options, CPU controls, roster rows and kick actions, start-button state, and
  the timed lobby update loop.
- Lifecycle: the owner is initialized once with `NetworkManager`,
  `TrackContentController`, and `LobbyChibiController`; it reloads after catalog
  scans, processes only while the lobby is visible, and clears presentation
  caches when the lobby closes.
- Static scene cleanup: removed the obsolete hidden `LobbyTrackSelector`,
  `PlayerList`, `StartRaceButton`, `AddCpuButton`, and `RemoveCpuButton` nodes
  from `main.tscn`. The real stable controls remain authored in
  `ui/lobby_static.tscn`; only data-driven roster and stage rows are created at
  runtime.
- Ownership support: reusable track-content evidence assembly moved to
  `TrackContentController`, serving both lobby and single-player options.
- Source result: `mxto/main.gd` lost 361 lines in this slice and contains no
  lobby widget fields or lobby presentation implementation.
- Verification: `scons target=template_release -j4` passed; Godot 4.7.1 opened
  the project and exited 0 without native load, script parse/load, scene, or
  startup errors. The known empty-texture and forced render-thread shutdown
  diagnostics remain.
- Deferred to Milestone 11: lobby empty-sequence, bumper, chibi, network-load,
  and netplay state-size tests updated for the direct owner.
- Commit: `e590e479` (`Extract lobby controller`).
- Remaining Milestone 5 work: race presentation and spectator ownership.

### Milestone 5b — spectator ownership complete

- Date: 2026-08-13
- New owner: the scene-owned `SpectatorController` in
  `mxto/ui/spectator_controller.gd`.
- Moved out of `GameManager`: eliminated/DNF input suppression, free-camera
  lifetime, eliminated-racer transition, live focus selection, controller
  strafe edge handling, camera toggling, and spectator notifications.
- Replay integration: `ReplayController` now uses the typed spectator owner to
  disable or position the shared free camera; it no longer creates or mutates a
  `GameManager` spectator node through dynamic calls.
- Lifecycle: race setup configures the local identity and racer/spectator role;
  menu, lobby, race-transition, and new-race paths reset the owner directly.
- Dead code removed: the uncalled visual-state elimination helper and its test-
  only production path were removed. The deferred vehicle-restore smoke now
  verifies the actual owner state used for elimination/DNF input suppression.
- Verification: `scons target=template_release -j4` passed; Godot 4.7.1 opened
  the project and exited 0 without native load, script parse/load, scene, or
  startup errors. The known empty-texture and forced render-thread shutdown
  diagnostics remain.
- Deferred to Milestone 11: the updated vehicle-restore/elimination smoke and
  replay camera coverage.
- Commit: `a3f7a64a` (`Extract spectator controller`).
- Remaining Milestone 5 work: race presentation ownership.

### Milestone 5c — race presentation ownership complete

- Date: 2026-08-13
- New owner: the scene-owned `RacePresentationController` in
  `mxto/ui/race_presentation_controller.gd`, with the stable
  `RaceResultsOverlay` authored as its scene child.
- Moved out of `GameManager`: result formatting and countdown, Grand Prix next-
  race machine setting, notifications, finish/KO medal feed, world stickers,
  nametag pooling, and placement badges.
- Direct consumers: `GameManager`, `ReplayController`, and `RaceHud` use the
  typed owner; no forwarding presentation API remains on `GameManager`.
- Authority boundary: finish placements, race times, eliminations, DNFs, Grand
  Prix points, and synchronized finish timing remain owned by the existing
  network and simulation systems. The new owner only formats and displays them.
- Source result: `mxto/main.gd` is 2,443 lines, down from roughly 4,900 at the
  start of the Godot ownership refactor; the cohesive presentation owner is 458
  lines.
- Static checks: the old `GameManager` presentation methods and state are gone,
  all callers target the new owner directly, and `git diff --check` passed.
- Verification: `scons target=template_release -j4` passed. A normal Godot 4.7.1
  launch reached the menu, and an automated single-player launch exercised race
  setup without native load, script parse/load, scene, access, or startup
  errors. Existing empty-texture, Steam-without-app-ID, and forced-shutdown
  diagnostics remain.
- Deferred to Milestone 11: Grand Prix results, replay presentation, stickers,
  nametags, medals, and related UI smoke coverage.
- Commit: `d9eaccd7` (`Extract race presentation controller`).

### Milestone 5 — UI ownership separation complete

- Lobby, spectator, and race-presentation behavior now have explicit scene-
  owned controllers and direct lifecycles.
- Stable lobby and race-results controls are scene-authored; dynamic creation is
  limited to data-driven roster rows, stage rows, medals, nametag pool entries,
  and placement badges.
- Milestone commits: `e590e479`, `a3f7a64a`, and `d9eaccd7`, with directly
  adjacent plan-status commits recording each verified slice.
- All affected tests remain deferred to Milestone 11 under the user-directed
  validation cadence.

### Milestone 6a — debug runtime ownership complete

- Date: 2026-08-13
- New owner: the scene-owned `DebugRuntimeController` in
  `mxto/core/debug_runtime_controller.gd`.
- Moved out of `GameManager`: render/phase profiling counters, top frame-gap
  samples, reports, active-script diagnostics, profiling CLI flags, rail and
  mesh trace configuration, bumper-smoke state, clean 4K screenshots, and
  diagnostic label visibility/text.
- Hot-path integration: `GameManager` retains the fixed process and physics
  order and records explicitly named phases into the owner only when profiling
  is enabled. Each `VisualCar` receives one race-start boolean so its profiling
  branches do not traverse the scene tree or controller graph per frame.
- Direct consumers: replay metadata, leaderboard eligibility, track visual
  profiling options, race HUD, and vehicle rendering read the new owner or the
  race-start boolean directly; old debug fields are absent from `GameManager`.
- Disabled behavior: the owner defines no `_process` or `_physics_process`; its
  profiling monitor reads and aggregation are skipped unless `--render-profile`
  is active.
- Source result: `mxto/main.gd` is 1,992 lines; the cohesive debug owner is 433
  lines.
- Verification: a normal bounded launch passed, automated single-player race
  setup passed, and an automated `--render-profile` race produced the expected
  native/GDScript profile summaries without access or parse errors. The exact
  `scons target=template_release -j4` build and post-build menu launch passed.
  Existing empty-texture, Steam-without-app-ID, audio forced-exit, and render-
  thread shutdown diagnostics remain.
- Deferred to Milestone 11: smoke and test suites, including transition and
  replay metadata coverage.
- Commit: `c983a5ec` (`Extract debug runtime controller`).
- Remaining Milestone 6 work: race-session setup/teardown and transitions.

### Milestone 6b — race session ownership complete

- Date: 2026-08-13
- New owner: the scene-owned `RaceSessionController` in
  `mxto/core/race_session_controller.gd`.
- Moved out of `GameManager`: race-world construction, player-controller and
  trigger lifetime, racer/content assembly, start-grid slots, native simulation
  instantiation, render-manager setup, Grand Prix start bonuses, and shared
  world destruction.
- State ownership: active player controllers, local racer index, trigger nodes,
  and last-race replay metadata now live with the session owner. Replay setup
  and restart paths call that owner directly; no `_start_race` compatibility
  method remains on `GameManager`.
- Transition ordering: `GameManager` still chooses menu, lobby, or next Grand
  Prix state. It calls the session owner's explicit audio/replay transition
  phase before pause/UI changes, then its world-destruction phase at the same
  point where the original code reset presentation, disconnected when needed,
  destroyed simulations, removed visuals/controllers, and restored 60 Hz.
- Duplication removed: menu, lobby, and next-race paths share the same actual
  destruction implementation while retaining their distinct network and UI
  state changes.
- Source result: `mxto/main.gd` is 1,592 lines, down from roughly 4,900 at the
  start of the Godot runtime refactor; the cohesive session owner is 396 lines.
- Deferred coverage: `track_scene_lobby_hide_smoke.gd` now asserts the new
  owner, absence of the old GameManager setup method, successful race setup,
  and lobby teardown. Existing replay, lobby-load, netplay, vehicle test-drive,
  and main-scene paths remain scheduled for Milestone 11.
- Verification: `scons target=template_release -j4` passed; the post-build
  automated single-player launch instantiated the race and exited 0 without
  native load, script parse/load, scene, or access errors. Baseline texture,
  Steam-without-app-ID, forced audio disconnect, and render-thread shutdown
  diagnostics remain.
- Commit: `af457a5f` (`Extract race session controller`).

### Milestone 6 — race lifecycle and debug ownership complete

- `GameManager` is now below the approximate 2,000-line target and retains
  application/race-state coordination plus the fixed tick/process entry points,
  rather than presentation, debug, content, or race-world implementations.
- Milestone commits: `c983a5ec` and `af457a5f`, with directly adjacent status
  commits recording the verified slices.
- All test suites remain deferred to Milestone 11 as directed.

### Milestone 7 — live protocol ownership map before edits

- Date: 2026-08-13
- Current source: `mxto/netplay/network_manager.gd`, approximately 3,700
  physical lines and 3,489 nonblank/content lines.
- Session/peer owner candidate: host/join/disconnect, peer callbacks, waiting-
  peer admission, kick/accept, and version negotiation. Version request/report/
  rejection currently use reliable default-channel RPCs. Human roster snapshots
  use authority reliable channel 7.
- Lobby owner candidate: human/CPU roster, CPU-ID collision repair, player-
  settings revisions and compressed snapshots, next-race acceleration setting,
  and lobby latency. CPU roster uses authority reliable default-channel RPC;
  player-settings snapshot/update and next-race acceleration use reliable
  channel 10; lobby latency ping/pong/snapshot use unreliable default-channel
  RPCs.
- Race-admission owner candidate: race phase acceptance, content-loading stages,
  stalled-peer status/drop policy, synchronized-start samples/scheduling, race
  start/end, spawn seed, and begin-simulation scheduling. Admission submission
  and drop request use reliable channel 7; stalled status uses unreliable
  channel 7; start-sync ping/pong/sample use unreliable channel 5; race
  start/end/begin-simulation use authority reliable channel 7.
- Input/timing transport owner candidate: local/pending/history input state,
  authoritative history, tick/ahead/RTT control, packet encoding, ack handling,
  prediction correction, and native `NetcodeSession` use. Client timing ping and
  server timing sync use unreliable channel 5; startup sync and client input use
  unreliable-ordered channel 1; server startup uses unreliable call-local
  channel 3; authoritative input broadcast uses unreliable-ordered call-local
  channel 2.
- State-transfer owner candidate: outgoing snapshot queue, chunk scheduling,
  incoming/FEC assembly, compression, restore handoff, and state-size evidence.
  State chunks currently use authority call-remote unreliable channel 4.
- Race-results/event owner candidate: synchronized finish time, finish/DNF/
  elimination state, force-end deadline, final placements, race options,
  presentation events, and sticker request cooldown. Finish time uses authority
  reliable channel 7; result/options/event RPCs are authority call-local
  reliable on the default channel; sticker request is any-peer reliable on the
  default channel.
- Telemetry owner candidate: CSV lifetime, interval counters, protocol byte and
  packet counts, timing/state/admission fields, lobby UI/network counters, and
  process-cost aggregation. It has no RPC authority and should read protocol
  owners one-way without driving their behavior.
- Cutover rule: move one complete RPC/state owner at a time, preserve every
  decorator and sender/phase check, change all node paths/callers in the same
  commit, and leave no forwarding endpoint on `NetworkManager`.
- First implementation slice: state transfer. Its state and channel-4 RPC are
  cohesive, it has a narrow restore handoff back to authoritative transport,
  and its packet format can remain unchanged while telemetry reads its counters
  directly.

### Milestone 7a — state-transfer ownership complete

- Date: 2026-08-13
- New owner: the scene-owned `StateTransferController` in
  `mxto/netplay/state_transfer_controller.gd`, beneath `NetworkManager` so the
  authority RPC resolves at the same stable node path on every peer.
- Moved out of `NetworkManager`: snapshot-recipient scheduling, compression,
  outgoing chunk pacing, channel-4 RPC receive, bounded metadata validation,
  FEC recovery, state assembly, pending/latest state tracking, and all state-
  transfer interval counters.
- Protocol preservation: state chunks remain authority, call-remote,
  unreliable channel 4. The phase bit, tick mask, 1,000-byte chunk payload,
  eight-data-chunk parity groups, three-chunk pacing, and 16 MiB payload bound
  are unchanged.
- Direct handoff: the owner signals completed snapshots to the existing
  authoritative restore path and exposes its latest restored tick directly to
  rollback coordination. Byte and sample signals feed existing telemetry and
  dump sinks; no forwarding RPC or compatibility alias remains on
  `NetworkManager`.
- Lifecycle: peer schedule rebuild, peer removal, race phase/activity, resets,
  interval logging, server snapshot creation, and outgoing sends now call the
  owner directly. The old transfer dictionaries and counters are gone.
- Source result: `mxto/netplay/network_manager.gd` is 3,278 lines, down from
  approximately 3,700 before Milestone 7; the cohesive state-transfer owner is
  459 lines.
- Deferred coverage: `netplay_state_transfer_smoke.gd` now targets the real
  owner and its state directly. The smoke and multi-process netplay matrix
  remain deferred to Milestone 11.
- Verification: `scons target=template_release -j4` passed. A post-build
  automated single-player launch opened the game, initialized a race, and
  exited 0 without native load, script parse/load, scene, or access errors.
  Existing empty-texture, Steam-without-app-ID, forced audio disconnect, and
  render-thread shutdown diagnostics remain.
- Commit: `ed342019` (`Extract network state transfer controller`).

### Milestone 7b — race-result and event ownership complete

- Date: 2026-08-13
- New owner: the scene-owned `RaceResultsController` in
  `mxto/netplay/race_results_controller.gd`, beneath `NetworkManager` with the
  same deterministic node path on every peer.
- Moved out of `NetworkManager`: synchronized race-finish time, finish/DNF/
  elimination records, placement normalization, final-result merge, force-end
  deadline, race presentation events, sticker request cooldown, and the related
  RPC endpoints.
- Protocol preservation: race-finish time remains authority call-remote
  reliable channel 7. Finish, DNF, elimination, final-result, deadline, and
  presentation-event RPCs remain authority call-local reliable on the default
  channel. Sticker requests remain any-peer reliable on the default channel.
  Phase-bit packing and stale-phase rejection are unchanged.
- Ownership handoff: a synchronous `player_dnf_recorded` signal leaves only the
  transport consequence—disconnected-racer input replacement and delayed-peer
  removal—with `NetworkManager`. Result state never calls back through a broad
  manager interface.
- Direct consumers: `GameManager`, replay capture/restore, race HUD,
  presentation, spectator behavior, and proximity voice read or mutate the real
  result owner. The old state, RPCs, methods, and race-event signal are absent
  from `NetworkManager`; no forwarding aliases remain.
- Source result: `mxto/netplay/network_manager.gd` is 3,074 lines; the cohesive
  result/event owner is 250 lines.
- Deferred coverage: Grand Prix/result normalization and vehicle-restore/
  elimination smokes now target the new owner directly. They and the affected
  replay, presentation, sticker, DNF, and multi-process RPC groups remain
  deferred to Milestone 11.
- Verification: `scons target=template_release -j4` passed. A post-build
  automated single-player launch opened the game, initialized a race, and
  exited 0 without native load, script parse/load, scene, or access errors.
  Existing empty-texture, Steam-without-app-ID, forced audio disconnect, and
  render-thread shutdown diagnostics remain.
- Commit: `7805c95b` (`Extract network race results controller`).

### Milestone 7c — lobby settings and CPU roster ownership complete

- Date: 2026-08-13
- New owner: the scene-owned `LobbySettingsController` in
  `mxto/netplay/lobby_settings_controller.gd`.
- Moved out of `NetworkManager`: player-settings storage/revisions, compressed
  settings snapshots and updates, next-race acceleration updates, CPU-ID
  allocation/remapping, CPU settings and lobby/race CPU rosters, CPU roster
  synchronization, lobby latency sampling/snapshots, and their interval
  counters.
- Protocol preservation: CPU roster sync remains authority call-remote reliable
  on the default channel. Settings snapshot/update and next-race acceleration
  remain reliable channel 10 with the same authority/sender checks, compression,
  size bounds, and livery merge behavior. Lobby latency ping/pong/snapshot
  remains unreliable on the default channel.
- Ownership handoffs: synchronous role-change and CPU-removal signals leave
  human player/spectator membership, desired-ahead transport state, pending-
  input cleanup, and snapshot scheduling with `NetworkManager`. A latency-
  sample signal preserves the existing transport RTT smoothing/peer RTT update
  without giving the lobby owner a manager back-reference.
- Direct consumers: lobby UI/chibis, car settings, replay, race setup/HUD,
  presentation, spectator, communication, proximity voice, custom stamps, and
  deferred load smokes target the real owner. No settings/CPU/latency forwarding
  methods remain on `NetworkManager`.
- Source result: `mxto/netplay/network_manager.gd` is 2,677 lines; the cohesive
  lobby settings owner is 452 lines.
- Deferred coverage: lobby chibi/render/scale/load, settings snapshot, CPU
  roster, latency, race setup, replay, and multi-process groups remain queued
  for Milestone 11.
- Verification: `scons target=template_release -j4` passed and a menu launch
  parsed/loaded the scene. The first automated race launch hit one intermittent
  native access violation during startup; two immediate identical reruns opened
  the game, initialized the race, and exited 0. Existing empty-texture, Steam-
  without-app-ID, forced audio disconnect, and render-thread shutdown
  diagnostics remain.
- Commit: `4593a5dc` (`Extract network lobby settings controller`).

### Milestone 7d — race admission and synchronized-start ownership complete

- Date: 2026-08-13
- New owner: the scene-owned `RaceAdmissionController` in
  `mxto/netplay/race_admission_controller.gd`, beneath `NetworkManager` at the
  same deterministic path on every peer.
- Moved out of `NetworkManager`: admission stages and progress evidence,
  stalled-peer reporting/drop policy, clock-sample state, start scheduling,
  synchronized client/authoritative start state, and first-authoritative-input
  startup telemetry.
- Protocol preservation: admission submission and stalled-player requests
  remain any-peer call-remote reliable channel 7; stalled status remains
  authority call-remote unreliable channel 7; clock ping/pong/sample remains
  call-remote unreliable channel 5 with the same authority and sender checks;
  immediate and scheduled start remain authority call-remote reliable channel
  7. Race-phase packing and stale-phase rejection remain local to the owner.
- Ownership handoffs: narrow synchronous signals update RTT smoothing, native
  peer-ahead state, transport clocks/ticks, simulation start flags, and peer
  disconnection. Race setup/teardown refreshes direct simulation and roster
  context; no manager back-reference or forwarding RPC remains.
- Direct consumers: race setup, the stalled-player UI, lifecycle/session code,
  load and state-size harnesses, Grand Prix transition coverage, and admission
  smoke coverage target the real owner directly.
- Source result: `mxto/netplay/network_manager.gd` is 2,251 lines; the cohesive
  admission/start owner is 478 lines.
- Deferred coverage: `netplay_admission_state_smoke.gd` now exercises the real
  owner directly. Admission, lobby-load, state-size, Grand Prix transition, and
  multi-process start/RPC groups remain queued for Milestone 11.
- Verification: `scons target=template_release -j4` passed. A normal bounded
  launch parsed and loaded the scene, and a post-build automated headless
  single-player launch initialized the race and exited 0 without native load,
  script parse/load, scene, or access errors. Existing Steam-without-app-ID and
  render-thread diagnostics remain.
- Commit: `b68e4749` (`Extract network race admission controller`).
