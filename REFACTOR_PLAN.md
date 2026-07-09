# Maxx Throttle subsystem refactor plan

## Objective

Break the largest source files into cohesive, domain-owned subsystems without
forwarding wrappers, compatibility layers, dead code, behavior changes, or
hot-path performance regressions. Call sites move to the new owners directly.

## Invariants

- The native simulation remains deterministic at 60 Hz.
- Replay files keep their current schema and build-signature rules.
- Replay recording, playback, seeking, cameras, catalog operations, debug
  replay, and strict verification retain their current behavior.
- Network RPC authority, transfer modes, race-phase checks, prediction, and
  rollback behavior do not change during structural extraction.
- Native tick paths gain no virtual dispatch, per-tick allocation, wrapper
  layers, or redundant state copies.
- Existing user-owned untracked files and unrelated worktree changes remain
  untouched.

## Phase 1: replay ownership

Create a scene-owned `ReplayController` and move all replay state and behavior
out of `GameManager`:

- real replay recording, serialization, loading, and result verification;
- playback startup, ticking, completion, seeking, and seek checkpoints;
- replay focus, timeline UI, playback rate, and replay cameras;
- replay catalog scanning, metadata, watch, rename, and delete operations;
- debug replay recording, loading, and playback;
- replay command-line options and replay-specific input handling;
- replay reset/teardown behavior shared by menu, lobby, and race transitions.

`GameManager` continues to own race creation, simulation stepping, track and car
content, results, and general UI. It calls the controller directly at the
actual lifecycle boundaries. `ReplayController` calls the existing race owner
directly where playback must start or end a race; no proxy methods are added.

Completion evidence:

- `mxto/main.gd` contains no replay implementation or duplicated replay state;
- all replay UI signal connections terminate at `ReplayController` methods;
- Godot can load the main scene and the focused replay smoke paths pass;
- a representative replay produces `MXT_REPLAY_VERIFY_OK` when a compatible
  local capture is available;
- packet roundtrip and netstate restore-equivalence tests still pass;
- the preferred native build and `git diff --check` pass.

## Phase 2: race audio ownership

Move music definitions, race-start/final-lap timing, announcer sequencing,
finish ducking, and UI one-shots into a `RaceAudioController`. Keep native
pooled spatial vehicle audio in `MxtSpatialAudioManager`. Remove the moved state
and update race lifecycle call sites directly.

## Phase 3: track catalog and loading ownership

Move external-track discovery, SHA-256 track identity, metadata parsing,
visual-scene resolution, and runtime visual loading into a track-content owner.
Keep native `.mxt_track` parsing and collision construction in `GameSim` and
`RaceTrack`.

## Phase 4: native GameSim source boundaries

Split existing `GameSim` method definitions across implementation files without
changing the class ABI or adding delegation:

- core lifecycle and fixed-tick orchestration;
- track/state instantiation;
- rollback and network-state serialization;
- render snapshots, multimeshes, effects, and camera submission;
- CPU driver input generation;
- bumpers, super sparks, and race events.

After the mechanical split, evaluate whether any group owns enough independent
state to become a direct component. Do not force components into the simulation
hot path merely to reduce line counts.

## Phase 5: netplay ownership

Separate connection/lobby state, race transport, start synchronization, state
chunk transfer, and telemetry. Preserve explicit RPC endpoints on their real
owning nodes and update all callers and RPC paths in one cutover; do not retain
old paths as aliases.

## Phase 6: vehicle physics source boundaries

Split `physics_car.cpp` only after the higher-level extraction and verification
contracts are stable. Candidate boundaries are floor/contact queries, motion
and controls, attacks/damage, restore/respawn, checkpoints, and vehicle
collisions. Preserve the existing SoA layout, lane workers, scratch storage,
and direct function calls.

## Working method

Each phase lands as small reviewable commits. Before and after each extraction:

1. inspect the live worktree and preserve unrelated changes;
2. run the preferred `scons target=template_debug debug_symbols=yes -j4` build
   when native sources are affected;
3. run focused Godot smoke scripts plus replay, packet, and netstate checks
   proportional to the changed boundary;
4. run `git diff --check` and inspect the complete staged diff;
5. remove obsolete state, methods, signals, and temporary migration code before
   committing.
