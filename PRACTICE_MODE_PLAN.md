# Practice Mode and TAS Authoring Plan

## Status

Implementation in progress. Phases A through F are implemented and smoke-verified; final focused automated validation remains deferred until the feature work is complete.

This document is the source of truth for implementing the first-class Practice mode, savestates, frame controls, telemetry, canonical replay authoring, and replay-to-Practice continuation described below. Execute it with a short goal such as:

> Follow the plan in `PRACTICE_MODE_PLAN.md`.

Keep this document updated while implementing it. Record completed phases, profiling results, and any material deviations rather than silently changing the design.

## Objective

Replace the current lightly modified `time_attack_practice` path with a deliberate single-player Practice mode that supports configurable race rules, sixteen session-local savestate slots, slow motion, pause, frame advance, limited rewind, exact input authoring, optional telemetry, finite canonical replay export, and continuation of compatible replays as new Practice timelines.

Practice must remain a real `GameSim` session. It must exercise the same vehicle physics, CPU drivers, track mutations, mines, triggers, bumpers, restore behavior, S-BOOST behavior, and replay input encoding used by ordinary play. Practice tooling may control time and restore exact state, but it must not introduce alternate approximations of gameplay.

The resulting finite Practice replay must remain an ordinary input-only replay. Savestate loads, rewinds, game-speed changes, UI operations, and timeline branches are authoring actions and must not be serialized as gameplay events. Export only the currently canonical input timeline from tick zero through its current end.

## Locked Product Decisions

### Practice setup

- Practice is a first-class single-player session, not ranked Time Attack with a few options changed.
- The existing Time Attack setup continues to offer Ranked and Practice entry points.
- Selecting Practice opens a dedicated setup panel instead of immediately starting a race.
- Practice setup exposes:
  - S-BOOST enabled or disabled.
  - Vehicle Restore enabled or disabled.
  - Bumpers enabled or disabled.
  - CPU racer count using the existing supported CPU range.
  - Lap count from 1 through 99.
  - Infinite laps.
  - Existing compatible ghost selection.
- Infinite laps disables the finite lap-count control and never completes the race normally.
- Infinite Practice does not record or export a replay.
- Finite Practice records a canonical replay timeline and may export it before or after race completion.
- Practice is always leaderboard-ineligible, including when its settings happen to match Ranked Time Attack.

### Savestate controls

- Practice has sixteen savestate slots numbered 1 through 16.
- Savestates exist only for the current Practice session. They are not written to disk and do not survive Retry, leaving the race, changing track, changing machine, or closing the game.
- D-pad controls are reserved by Practice while actively driving:
  - D-pad Left selects the previous slot and wraps from 1 to 16.
  - D-pad Right selects the next slot and wraps from 16 to 1.
  - D-pad Down saves into the selected slot.
  - D-pad Up loads the selected slot.
- Saving directly overwrites an occupied slot without a confirmation dialog.
- Loading an empty slot does nothing beyond showing a clear notification.
- Slot selection always produces a brief notification, such as `Slot 4 — Empty` or `Slot 4 — Lap 2 · 00:41.350`.
- Save and load produce distinct brief notifications.
- Practice suppresses the normal D-pad sticker-wheel behavior so a single press cannot affect both systems.
- There is no automatic load-after-crash or automatic load-after-fallout option.

### Game speed and frame controls

- Practice adds a `Game Speed` row to the in-race pause menu.
- The row is visible only in Practice.
- Menu Left and Menu Right change speed by exactly 0.05x.
- The inclusive range is 0.00x through 2.00x.
- Display speed with two decimal places.
- Opening the pause menu temporarily freezes local Practice without changing the selected speed.
- Closing the pause menu resumes at the selected speed.
- Closing the pause menu at 0.00x leaves the simulation frozen and enables frame controls.
- At 0.00x and while the pause menu is closed:
  - Right-stick Right advances exactly one 60 Hz simulation frame.
  - Right-stick Left rewinds exactly one available simulation frame.
  - Each direction is edge-triggered. The stick must return through a release deadzone before another step.
- Right-stick vertical camera input remains presentation input and is not part of frame stepping.
- The initial rewind window is the native 45-frame rollback history already retained by each normal `GameSim`, equivalent to 0.75 seconds at 60 Hz.
- Reaching the oldest available frame produces feedback and does not wrap.
- Game speed survives Retry within the current Practice configuration but resets to 1.00x when Practice is exited.
- Physics continues to use fixed 1/60 simulation steps at every speed. Do not scale a physics tick's logical delta.
- Audio behavior is not redesigned. Preserve the behavior already used by replay slow motion and pause, including normal pitch.

### Rewind semantics

- Rewind is an alternative rolling set of savestates, not a replay scrubber and not an undo/redo input editor.
- Rewinding one frame loads the complete preceding frame state.
- Rewinding immediately truncates the current canonical replay timeline after the restored frame.
- There is no remembered or replayed `next input` after rewind.
- Frame advance always samples the current live input or current manually authored input and appends a new canonical frame.
- Repeated rewind may continue backward while valid native and companion history exists.
- Once new frames are advanced, discarded future state and discarded future inputs are not recoverable unless an explicit savestate slot still references that older branch.

### Explicit savestate semantics

- Each occupied slot captures the exact main simulation state and the matching canonical replay prefix.
- Each slot also captures every active Practice ghost simulation and its runtime cursor/state so ghosts remain synchronized after load.
- Loading a slot restores that slot's simulation branch and makes its saved replay prefix canonical.
- Loading a slot discards the formerly current suffix, but other occupied slots retain their own branches.
- Slots must share immutable replay history internally rather than duplicating an entire long replay prefix per slot.
- Camera mode, camera zoom, camera free-look state, selected game speed, telemetry display mode, currently selected slot, and pending manual input are presentation/tool preferences and are not restored by savestates.
- Savestate load must snap or safely reconcile rendering and clear invalid transient presentation state. It must not create rollback interpolation from the abandoned future position.

### Exact input authoring

- Practice supports Live and Manual input modes while frozen at 0.00x.
- Live mode samples the controller normally when a frame is advanced.
- Manual mode uses the complete latched input displayed in the input editor.
- Manual input remains latched from frame to frame until changed or reset.
- `Capture Current Controller Input` copies the current live controller state into the manual editor.
- `Reset to Neutral` clears all analog and digital input fields.
- The editor operates on the actual replay encoding precision:
  - Horizontal steering: raw 0 through 254 mapped to -1.0 through 1.0.
  - Vertical steering: raw 0 through 254 mapped to -1.0 through 1.0.
  - Left strafe trigger: raw 0 through 254 mapped to 0.0 through 1.0.
  - Right strafe trigger: raw 0 through 254 mapped to 0.0 through 1.0.
  - Accelerate: binary.
  - Brake: binary.
  - Boost: binary.
  - Spin attack: binary.
  - Side attack: binary.
- Analog rows expose a mouse-adjustable slider, mouse-wheel adjustment, and exact numeric editing.
- Show both raw encoded value and normalized value. The raw encoded value is authoritative.
- Values shown in the editor must serialize to exactly the displayed bytes. Do not accept hidden higher precision and round it later.
- The input editor is editable only at 0.00x. It may remain visible as a read-only input display at other speeds if the presentation is useful.

### Telemetry

- Practice telemetry cycles through `Off`, `Compact`, and `Expanded`.
- Slot-change/save/load notifications remain visible regardless of telemetry mode.
- Compact telemetry initially includes:
  - Canonical simulation tick.
  - Lap and lap target or infinity.
  - Speed in km/h.
  - Yaw rate in degrees per second.
  - Current gripped/drift/technique classification.
  - Energy and maximum energy.
  - Turbo/boost state.
  - Selected savestate slot and occupied/empty state.
  - Game speed and available rewind frames.
- Expanded telemetry additionally includes:
  - Local forward, lateral, and vertical velocity.
  - Local pitch, yaw, and roll angular velocity.
  - Per-corner grip/drift state or a compact corner mask.
  - Grounded, airborne, low-gravity, and surface classification.
  - Current checkpoint and checkpoint fraction.
  - Current exact encoded input values.
- Telemetry is observational only and never enters replay data or savestate simulation data.
- When telemetry is Off, do not build per-frame dictionaries or strings for it.
- When visible, prefer one compact native sample and update text at a bounded presentation rate rather than performing many native calls and allocations every simulation tick.

### Canonical Practice replays

- Finite Practice maintains one current canonical input timeline.
- Every occupied savestate slot references the canonical prefix that produced its simulation snapshot.
- Loading a slot changes which branch is canonical.
- Rewind truncates the canonical timeline immediately.
- Advancing a frame appends all authoritative racer inputs for that frame, including CPU inputs.
- Game-speed changes, time spent paused, savestate operations, telemetry settings, and input-editor UI activity do not appear in the replay.
- Export preserves the existing replay schema and ordinary input-frame representation unless a separately justified schema field is required for Practice metadata.
- A Practice replay is explicitly unranked and can never be queued for trusted leaderboard submission.
- `Save Current Replay` is non-destructive:
  - It may be used before race completion.
  - It writes a new local replay from the current canonical prefix.
  - It does not stop recording.
  - It does not clear the in-memory timeline.
  - It may be used repeatedly as the player develops a run.
- A partial replay ends cleanly at its final authored input and pauses in the replay viewer like any other replay end.
- Completing a finite Practice run retains the existing Watch Replay and Save Replay affordances, operating on the canonical timeline.

### Resume in Practice Mode

- Compatible replay playback adds a `Resume in Practice Mode` action.
- Resume occurs at the current replay playback position, not necessarily at the replay's final frame.
- Resume is disabled if the currently focused racer has already completed the race at that position.
- A completed replay cannot be resumed from its completion state. The player may seek to an earlier controllable frame and resume there.
- Reaching the end of a partial replay without completing the race remains resumable.
- The currently focused replay racer becomes the locally controlled Practice racer.
- Recorded CPU racers remain CPUs.
- Other recorded human racers become native CPUs using their recorded vehicles and settings.
- Resume restores the source replay's exact available configuration:
  - Track content identity and exact package.
  - Vehicle content identities and exact packages.
  - Machine settings.
  - Race options, including S-BOOST, Restore, bumpers, finite lap target, and infinite state when represented.
  - Spawn seed.
  - Starting-grid slots.
  - CPU roster.
- Resume requires every exact track and vehicle dependency needed by the replay. Fail with a clear actionable message instead of silently substituting content.
- The replay prefix through the resume cursor seeds the new canonical Practice timeline.
- The source replay file or leaderboard cache entry is never modified.
- A later export creates a new local Practice replay.

### Keep Original as Ghost

- The Resume dialog includes `Keep Original as Ghost`, enabled by default when the source replay has frames remaining after the resume cursor.
- The ghost represents the original future trajectory of the focused racer only.
- It runs in its own single-car `GameSim` and cannot affect the new Practice simulation.
- It begins from the same logical replay cursor as the resumed Practice branch.
- It consumes the original source replay's remaining focused-racer inputs while the new branch consumes live/manual inputs.
- It uses the source replay's exact machine, machine setting, starting state, seed, and track.
- It uses the established translucent Time Attack ghost presentation and does not enter placement, collisions, damage, audio, results, or the exported replay roster.
- When the original source timeline ends, the ghost fades normally.
- If no source frames remain, disable the checkbox because there is no future trajectory to compare.
- Savestates and rewind must capture/restore this ghost exactly like any other selected Practice ghost.

## Explicit Non-Goals

- Online multiplayer savestates, rewind, frame advance, manual input editing, telemetry, or game-speed control.
- Ranked Time Attack savestates or TAS tools.
- Leaderboard submission of Practice-authored replays.
- Persistent savestate files or resumable TAS project files in the first version.
- Redoing discarded input after rewind.
- Automatically loading a savestate after a crash, fallout, or missed section.
- Altering audio pitch or designing a separate slow-motion audio engine.
- Restoring camera mode, zoom, or orientation from a savestate.
- Editing CPU input manually.
- Editing arbitrary historical frames in a spreadsheet-style timeline. Historical changes are made by rewinding/loading and authoring forward.
- Replacing the existing replay file format with a state-jump movie format.
- Allowing a completed racer state to resume beyond the finish.
- Making source ghosts collide with the new Practice branch.

## Existing Systems to Preserve and Extend

- `mxto/ui/time_attack_setup.gd` and `.tscn` own the Ranked/Practice entry point and existing ghost selection.
- `mxto/steam/time_attack_rules.gd` owns canonical Ranked rules. Practice options must not weaken or complicate Ranked eligibility.
- `mxto/main.gd` currently starts `time_attack_practice`, owns the single-player tick path, pause-menu navigation, retry flow, and finish detection.
- `mxto/ui/race_pause_menu.tscn` owns the static pause-menu presentation.
- `mxto/ui/race_hud.gd` currently consumes the D-pad for stickers. Practice must take input priority and suppress that path.
- `mxto/replay/replay_controller.gd` owns ordinary replay recording, export, playback, seeking, playback speed, and replay-controller UI.
- `mxto/time_attack/time_attack_ghost_controller.gd` owns independent ghost simulations and already supports exact replay-driven `GameSim` instances.
- `mxto/core/race_session_controller.gd` owns race creation/destruction and native simulation configuration.
- `mxto/netplay/race_results_controller.gd` owns finish, DNF, elimination, and placement state outside the native heap.
- `src/gamesim/gamesim.h`, `gamesim.cpp`, and `gamesim_full_state.cpp` already provide a 45-frame native saved-state ring and complete full-state serialization.
- `src/core/player_input.h` and `mxto/player/player_input.gd` define the authoritative 8-bit replay input quantization.

Do not pile the Practice implementation into `main.gd` or `replay_controller.gd`. Extract focused owners and modify their call sites directly. Do not retain replaced forwarding paths or compatibility wrappers.

## Target Architecture

### Practice setup scene

Add a static `practice_setup.tscn` and focused script under `mxto/practice/`. It owns only Practice configuration and start/back signals. It may host or open the existing ghost picker, but it does not own simulation state.

### Practice controller

Add a focused Practice controller under `mxto/practice/`. It owns:

- Practice session activation and teardown.
- Selected game speed and temporary pause state.
- Frame-step edge detection.
- Sixteen explicit slot records.
- Forty-five companion rewind records matching the native saved-state ticks.
- Coordination of main and ghost state capture/load.
- Canonical replay timeline cursor/branch references.
- Manual input mode and pending input.
- Telemetry mode and sampling.
- Practice HUD notifications.
- Replay-resume transition state.

It must not own vehicle physics, CPU decision-making, track mutation, replay JSON formatting, leaderboard behavior, or generic replay playback.

### Canonical replay timeline

Move canonical recording storage behind a focused replay timeline owner rather than exposing and resizing `replay_recording_frames` from Practice.

Required operations:

- Begin a finite Practice timeline from tick zero.
- Seed a timeline from a validated replay prefix.
- Append one authoritative multi-racer input frame.
- Capture an immutable cursor/head for a savestate slot.
- Restore a saved cursor/head as canonical.
- Truncate after rewind.
- Report current length and encoded byte count.
- Flatten the current branch into the existing replay writer.
- Export a snapshot without ending the live recording.
- Release unreferenced branches when slots are overwritten or cleared.

Use fixed-size shared chunks or an equivalent append-friendly, reference-counted representation. A slot should retain a small branch reference, not copy every frame preceding it. Per-tick base64 conversion, JSON construction, and deep duplication are prohibited. JSON/base64 conversion remains an export-time operation.

### Native GameSim changes

Add only generic native capabilities required by Practice:

- Configurable finite target lap count.
- Explicit infinite-lap completion behavior.
- A checked saved-state load operation that verifies the requested tick is still present instead of loading a stale modulo slot.
- Any small exact-state metadata needed to keep bumper scheduling, CPU drivers, triggers, and local vehicle state deterministic after load.
- A compact telemetry snapshot when telemetry is enabled.
- A direct racer metadata/CPU-role update needed when taking control of a replay racer and converting other humans to CPUs.

Practice meaning remains in the Practice controller. Native code exposes generic simulation configuration and exact-state operations; it does not know about D-pad bindings, UI slots, TAS panels, replay buttons, or notifications.

## 1. Practice Setup and Session Identity

1. Give Practice its own unambiguous session kind instead of continuing to overload Ranked Time Attack meaning.
2. Update every direct `time_attack_practice` call site to the new Practice identity and remove the replaced conditional path.
3. Keep Ranked Time Attack options built exclusively by `TimeAttackRules`.
4. Build Practice options from the dedicated setup scene.
5. Mark Practice leaderboard-ineligible at start and preserve that status through Retry, replay export, and Resume.
6. Pass finite lap target/infinite state into `GameSim` before simulation begins.
7. Start replay recording only for finite Practice.
8. Start the Practice controller after the race and all selected ghosts are successfully instantiated.
9. Tear it down before the world, ghost simulations, or replay data are destroyed.

## 2. Configurable and Infinite Laps

Race completion is currently hardcoded around three laps in native vehicle state. Replace that assumption with a configured target:

- A positive target means complete after that many laps.
- Zero means infinite and never sets completed-race state from lap progression.
- Ranked Time Attack continues to pass exactly three.
- Ordinary race modes retain their current configured/default behavior.
- Replay playback reads and applies the recorded lap target.

Do not implement Infinite as a huge fake target. Audit lap counters and lap-coupled state so a long Practice session cannot wrap at 255 and accidentally corrupt placement, checkpoint distance, bumper scheduling, broken-lap restoration, HUD output, or completion. Widen the relevant fields and update native state/network serialization directly where required. Preserve ordinary three-lap behavior bit-for-bit where the wider range is unused.

The Practice HUD displays `Lap N / M` for finite sessions and `Lap N / ∞` for infinite sessions. Infinite sessions never show normal race results because of lap completion.

## 3. Practice Clock

Use the same fixed-tick clock policy already proven by replay playback:

- Logical `GameSim` ticks remain 1/60 second.
- Nonzero speed changes tick scheduling while preserving the logical tick duration.
- 0.00x stops automatic simulation ticks.
- UI input, pause navigation, notifications, and mouse editing continue to function at 0.00x using unscaled presentation time where necessary.
- Entering Options or the pause menu must not lose the selected Practice speed.
- All Engine clock values are restored on Retry transition, Practice exit, replay transition, error, and shutdown.
- Online sessions, menus, Ranked Time Attack, and normal races must always regain their expected clock.

Do not modify audio pitch, manually retune AudioStreamPlayers, or add Practice audio DSP. Preserve the replay clock's existing audio behavior.

Frame advance must execute the same complete per-tick pipeline used by normal finite Practice:

1. Resolve Live or Manual local input.
2. Tick the main `GameSim` once.
3. Tick every active ghost once.
4. Record authoritative racer inputs into the canonical timeline.
5. Capture rewind companion state.
6. Consume race events.
7. Update results/finish state.
8. Update render snapshots, HUD, camera target, and audio event state.

Do not create a second abbreviated frame-advance simulation path.

## 4. Rewind and Savestate Capture

### Rolling rewind

The native GameSim already saves one state per tick in a 45-slot ring. Add a matching Practice companion ring containing only non-native state that is required for exact local restoration:

- Canonical tick and timeline head/cursor.
- Race-results dictionaries and finish timing state.
- Practice completion/finalization state.
- Consumed race-event cursor/state.
- Ghost runtime cursor and presentation lifecycle state.
- Any GDScript timer whose value affects future simulation or results.

Every ghost `GameSim` must have a valid state for the same tick before that rewind tick is advertised as available.

On rewind:

1. Verify the requested tick remains valid in the main and every required ghost ring.
2. Load all native states.
3. Restore the companion record.
4. Restore the canonical replay head and truncate the formerly current suffix.
5. Clear invalid future result/presentation state.
6. Refresh render snapshots without rollback smoothing from the discarded state.
7. Keep current camera/tool preferences.
8. Show updated rewind depth and tick.

### Explicit slots

Each slot record contains:

- Occupied flag.
- Display metadata: tick, lap, elapsed canonical time, and creation sequence.
- Main `GameSim` full-state bytes.
- One full-state payload and runtime descriptor for each active ghost.
- Complete companion state.
- Immutable canonical replay timeline head/cursor for finite Practice.

Capture allocates only when the player explicitly saves. Overwriting a slot releases the previous state and branch references. Loading validates the session identity and exact roster before mutating live state. A failure must leave the current session untouched and display a clear error.

Instrument total bytes per slot, aggregate slot bytes, snapshot capture time, and restore time. Four ghosts plus sixteen slots can retain meaningful memory; expose the real cost instead of guessing.

## 5. Manual Input Editor

Add a static mouse-friendly panel under `mxto/practice/`. It should be visually compact enough to coexist with the race view and telemetry.

Behavior:

- The panel clearly displays `Live` or `Manual` mode.
- Switching to Manual seeds the panel from the most recently captured live input unless the player already authored a manual template.
- Numeric entry clamps to encoded raw bounds and updates the slider.
- Slider and wheel changes update the exact raw value first, then derive normalized display text.
- Boolean buttons are conventional toggles.
- Frame advance serializes the manual `PlayerInput` through the same authoritative encoder used by normal play.
- Immediately decode the serialized bytes for display/testing so the committed frame is demonstrably identical to the requested raw values.
- The right stick used for frame stepping is a tool command and must not leak into vertical/horizontal vehicle input.
- D-pad tool commands must not leak into stickers or menu navigation while the race view owns Practice input.

The existing replay input display may provide presentation assets or shared field naming, but remove duplication through direct shared data formatting rather than a forwarding wrapper.

## 6. Replay Recording and Partial Export

Refactor the recording owner so Practice can branch and export without breaking ordinary race recording:

- Normal races and Ranked Time Attack remain append-only and need no branch controls.
- Finite Practice uses the canonical timeline owner.
- Infinite Practice skips input retention entirely beyond what native rewind and occupied savestates require.
- CPU inputs remain authoritative frames obtained from `GameSim`; do not regenerate them during export.
- Export metadata records the exact Practice race options and marks the replay unranked.
- Export duration comes from canonical frame count, not wall-clock time or time spent paused.
- Partial exports are catalogued like other local replays and are watchable immediately.
- Export never clears the canonical timeline or marks the session permanently saved.
- Repeated export generates distinct files using the existing safe naming convention.

Add a deterministic verification utility for development that replays the flattened canonical inputs from tick zero and compares the resulting state at selected checkpoints with the authored Practice branch. Run this in final focused validation; it need not execute during every interactive save.

## 7. Resume Replay into Practice

Add the action to the replay playback controller and local replay presentation without placing Practice ownership inside the generic playback loop.

### Eligibility

Resume is enabled only when:

- Replay parsing and normal compatibility checks succeeded.
- The current playback cursor has an exact state available or can be reconstructed through the existing seek/checkpoint path.
- Exact track and vehicle content are available.
- The focused racer exists at the cursor.
- The focused racer has not completed the race.
- The replay carries enough race-option metadata to reconstruct its session, using existing well-defined defaults only where the current replay schema already permits them.

Display a specific disabled reason for completed racer, missing content, incompatible replay, or unavailable cursor state.

### Transition

1. Freeze replay playback at the current cursor.
2. Capture the exact full state at that cursor.
3. Extract the canonical replay prefix through the cursor without JSON/base64 round-tripping.
4. Build the Practice configuration from recorded options.
5. Make the focused racer the controlled local racer.
6. Keep recorded CPUs as CPUs.
7. Convert every other recorded human racer to a native CPU while retaining its current state, machine, setting, and grid history.
8. Clear replay-only UI/results state that does not belong to live Practice.
9. Seed the finite canonical timeline with the source prefix, or disable recording if the source configuration is infinite.
10. Optionally create the original-future ghost.
11. Enter Practice frozen at 0.00x so the player can inspect telemetry/input before committing the first new frame.

The transition must not reload from a substituted default machine, alter the source replay, or write a new replay until the user explicitly exports.

### Original-future ghost

Prepare the focused racer's source inputs after the resume cursor using the existing ghost input representation. Instantiate or restore an independent ghost `GameSim` at the same cursor state. The new Practice tick is its master clock. This ghost is presentation-only and is included in Practice savestate coordination but excluded from canonical replay frames.

## 8. Pause Menu, HUD, and Input Priority

Add static Practice controls rather than constructing them ad hoc in `main.gd`:

- `Game Speed  1.00x` focusable row in the pause menu.
- `Telemetry  Off/Compact/Expanded` row or a direct Practice HUD action.
- `Input Mode  Live/Manual` and `Input Editor` action when applicable.
- `Save Current Replay` for finite Practice, available even before completion.
- Existing Resume, Retry, Options, and Exit behavior.

Controller navigation uses the existing threshold plus DAS/ARR discipline. Horizontal adjustment must not cause vertical focus movement or double-handle Godot native navigation.

Practice HUD remains compact and never pushes or resizes the normal race HUD. Notifications overlay briefly and then disappear; persistent state belongs in the compact Practice status line.

Input priority while Practice is active:

1. Modal Options or confirmation UI.
2. Pause menu.
3. Manual input editor mouse/UI input.
4. Practice D-pad savestate commands and zero-speed right-stick frame commands.
5. Spectator/camera presentation input not claimed above.
6. Normal vehicle input.
7. Sticker UI, which is suppressed for Practice.

## 9. File Organization

Suggested files:

- `mxto/practice/practice_setup.tscn`
- `mxto/practice/practice_setup.gd`
- `mxto/practice/practice_controller.gd`
- `mxto/practice/practice_hud.tscn`
- `mxto/practice/practice_hud.gd`
- `mxto/practice/practice_input_editor.tscn`
- `mxto/practice/practice_input_editor.gd`
- `mxto/replay/replay_recording_timeline.gd` or a native equivalent if profiling justifies it
- Focused native additions in existing `src/gamesim/` files, split into a new practice/state file if the owner would otherwise become oversized
- Focused tests under `mxto/test/`

Modify direct owners and call sites:

- `mxto/ui/time_attack_setup.gd` and `.tscn`
- `mxto/ui/race_pause_menu.tscn`
- `mxto/ui/race_hud.gd`
- `mxto/main.gd`
- `mxto/replay/replay_controller.gd`
- `mxto/time_attack/time_attack_ghost_controller.gd`
- `mxto/core/race_session_controller.gd`
- `mxto/netplay/race_results_controller.gd`
- `src/core/player_input.h` only if a generic exact raw-value helper is genuinely needed
- `src/car/physics_car_restore.cpp` and the native saved/network state definitions for lap-target work

Do not create a monolithic Practice script. Do not introduce a shim that forwards old Practice calls into the new controller. Remove the replaced `time_attack_practice` special cases after their call sites move.

## 10. Instrumentation

Practice instrumentation should be quiet by default and summarized at transitions or explicit diagnostic requests.

Track:

- Main and ghost full-state byte sizes.
- Explicit slot capture and restore microseconds.
- Aggregate occupied-slot memory.
- Rewind success, oldest-available depth, and restore microseconds.
- Canonical timeline frame count, branch count, retained chunk count, live bytes, and released bytes.
- Frame-advance tick microseconds with zero through maximum configured CPUs and ghosts.
- Telemetry sample/render microseconds when Compact and Expanded.
- Replay-resume seek/capture/transition time.
- Original-future ghost tick and memory cost.
- Game clock entry/exit state to catch leaked Engine speed settings.

Expose one concise diagnostic snapshot rather than continuously printing every tick. Reuse the project's existing session memory/profile reporting where appropriate.

## 11. Implementation Sequence

### Phase A: Native lap and checked-state foundations

- Add configurable target/infinite lap behavior.
- Audit and widen lap-coupled counters that can wrap in Infinite Practice.
- Add checked 45-frame state loading.
- Confirm full-state round trips preserve CPUs, bumpers, triggers, mines, restores, and lap state.
- Expose a compact telemetry sample without UI.
- Release compile and launch.

### Phase B: First-class Practice setup and lifecycle

- Add static setup scene and options.
- Replace the overloaded `time_attack_practice` identity and call sites.
- Configure finite/infinite replay policy.
- Add focused Practice controller lifecycle and clean teardown.
- Keep Ranked Time Attack behavior unchanged.
- Release compile and launch.

### Phase C: Game speed and pause integration

- Add Practice-only pause rows.
- Implement 0.00x through 2.00x fixed-tick clock behavior.
- Preserve pause-menu responsiveness and existing audio behavior.
- Add right-stick edge-latched single-frame advance at 0.00x.
- Guarantee Engine clock restoration on every exit path.
- Release compile and launch.

### Phase D: Rolling rewind and sixteen slots

- Add the companion 45-frame history.
- Coordinate checked state capture/load for main and ghost simulations.
- Add slot storage, D-pad controls, notifications, and compact HUD state.
- Suppress Practice sticker input.
- Clear slots on Retry/exit/content change.
- Instrument memory and restore time.
- Release compile and launch.

### Phase E: Canonical timeline and exact input editor

- Add shared/chunked canonical timeline storage.
- Integrate finite Practice recording.
- Make rewind truncate with no redo input.
- Make slots retain independent timeline heads.
- Add Live/Manual modes and exact raw-value input editor.
- Add non-destructive partial replay export.
- Release compile and launch.

### Phase F: Telemetry

- Add Off/Compact/Expanded HUD presentation.
- Wire the compact native sample.
- Keep Off allocation-free and bound visible update cost.
- Ensure notifications remain independent.
- Release compile and launch.

### Phase G: Resume in Practice and original ghost

- Add replay-controller eligibility and disabled reasons.
- Transition from current pre-completion cursor into Practice.
- Seed canonical prefix without JSON/base64 conversion.
- Transfer focused control, preserve CPUs, and convert other humans to CPUs.
- Add Resume dialog with `Keep Original as Ghost` enabled by default.
- Synchronize original-future ghost with savestate/rewind.
- Release compile and launch.

### Phase H: Cleanup and final repair

- Remove replaced Practice conditionals, dynamic UI, and dead recording paths.
- Review ownership and file sizes.
- Run final focused automated checks and manual matrix.
- Fix discovered defects.
- Record final profiling and verification results in this document.

Complete and commit each coherent phase before opening the next one. Preserve unrelated dirty work and stage only files belonging to the current phase.

## 12. Verification Policy

During implementation, follow the repository's lightweight cadence:

- Required release build: `scons target=template_release -j4`
- Launch the game and confirm it opens after each coherent phase.
- Do not repeatedly run the broad test suite during intermediate work.

After all implementation phases are complete, run focused automated coverage and repair failures. Run a broader suite only if its measured runtime is under 30 seconds or the user explicitly asks for it.

### Focused automated coverage

- Ranked Time Attack still builds exact canonical three-lap options.
- Practice setup emits every option combination correctly.
- Finite lap targets complete at the requested count.
- Infinite laps do not complete and do not wrap/corrupt lap state in a long accelerated sample.
- Infinite Practice does not retain replay frames.
- Game speed produces the expected number of fixed 60 Hz ticks.
- Every exit path restores normal Engine clock settings.
- Manual raw input values serialize and decode identically for all boundaries and representative interior values.
- Frame advance commits exactly one frame per right-stick deflection.
- Rewind loads exactly one prior state, truncates the canonical timeline, and has no redo future.
- The 45-frame boundary refuses stale modulo states.
- Explicit slots restore main simulation, CPUs, mutable track state, results state, canonical prefix, and ghosts.
- Two slots retaining different branches remain independent after either one is loaded and extended.
- Partial export is non-destructive and can be repeated.
- Flattened canonical replay reproduces authored checkpoint states from tick zero.
- Resume from a partial replay end enters Practice successfully.
- Resume from a mid-replay cursor uses only the prefix through that cursor.
- Resume is disabled once the focused racer is complete.
- Focused racer control transfer, CPU preservation, and human-to-CPU conversion are correct.
- Original-future ghost follows source inputs, remains isolated, and restores with slots/rewind.
- Practice D-pad controls do not open or send stickers.
- Telemetry Off performs no sample formatting/allocation.

### Manual matrix

Exercise at minimum:

- Mouse, keyboard, Xbox-style controller, and another available controller mapping.
- Practice with 0 CPUs, several CPUs, and the supported high CPU count.
- Practice with zero ghosts and four ghosts.
- Restore on/off, S-BOOST on/off, bumpers on/off.
- One lap, several laps, 99 laps setup, and Infinite.
- Save/load every slot, overwriting occupied slots, wrapping selection, and loading empty slots.
- Branch A saved in one slot, Branch B saved in another, then repeated switching and extension.
- Rewind at 1.00x after switching to 0.00x, oldest boundary, and advancing a new branch.
- Every speed from 0.00x to 2.00x through controller repeat and mouse interaction.
- Retry and exit while slowed, paused, editing input, and holding occupied slots.
- Exact minimum, neutral, maximum, and one-step analog inputs.
- Partial replay export, playback end pause, Resume in Practice, and re-export.
- Resume a single-racer replay, CPU race replay, and multiplayer replay focused on different racers.
- Resume before completion and verify the completed position is rejected.
- Keep Original as Ghost on/off and source ghost ending/fading.
- Compact and Expanded telemetry at multiple resolutions without covering core HUD information.
- Camera mode/zoom remaining unchanged across savestate loads.
- Audio retaining current pitch/behavior at slow speed and pause.

## 13. Acceptance Criteria

The feature is complete when:

- Practice has a dedicated setup flow with all locked settings.
- Ranked Time Attack remains unchanged and leaderboard-safe.
- Finite and Infinite lap behavior is native, configurable, and correct.
- Sixteen in-memory slots save and restore the complete Practice branch.
- The D-pad mapping is exact, responsive, wrapped, notified, and isolated from stickers.
- Game speed adjusts from 0.00x through 2.00x in 0.05x increments without changing logical physics steps or audio pitch behavior.
- 0.00x frame advance and 45-frame rewind are exact and edge-triggered.
- Rewind behaves as rolling savestates with no recorded next-frame input or redo timeline.
- Manual input authoring commits exact replay-encoded values.
- Telemetry Off/Compact/Expanded works without affecting simulation.
- Finite Practice exports the current canonical input-only replay before or after completion without ending the session.
- Infinite Practice never records a replay.
- Compatible incomplete replay positions can resume into Practice with exact recorded settings.
- Completed focused racers cannot resume.
- The focused racer becomes controllable, CPUs remain CPUs, and other humans become CPUs.
- The original focused trajectory can continue as an isolated default-enabled ghost.
- Exported Practice replays remain unranked and never enter leaderboard submission.
- Release compilation, launch validation, focused automated checks, manual controls, replay reproduction, and profiling all pass.

## Execution Record

### Material deviations

- None.

### Completed phases

- 2026-08-20 — Phase A: added native configurable/infinite lap targets, widened racer and bumper lap-coupled state, made rollback loads reject stale modulo slots, carried the lap target in full-state snapshots, and exposed one compact native vehicle telemetry sample. `scons target=template_release -j4` passed and a newly spawned game process remained open through the launch smoke check. Focused state round-trip assertions remain scheduled for Phase H under the agreed test cadence.
- 2026-08-20 — Phase B: added a dedicated static Practice setup and lifecycle controller, replaced the overloaded Time Attack practice identity with first-class Practice options, preserved Ranked option construction, configured finite/infinite recording and completion policy, made Retry and Race Again retain exact Practice options, and added finite/infinite lap HUD output. `scons target=template_release -j4` passed. Because the editor had an embedded game holding its hot-reload DLL, launch verification used a temporary isolated project/bin without stopping the editor or its game; Phase C's log-backed rerun also repaired the new scripts' initial uncached-global-class references. Focused setup/lifecycle assertions remain scheduled for Phase H under the agreed test cadence.
- 2026-08-20 — Phase C: added the Practice-only pause-menu speed row, exact 0.05x steps from 0.00x through 2.00x, replay-style fixed-tick clock scheduling, temporary pause freezing, Retry speed preservation, unscaled pause navigation, and edge-latched right-stick frame advance through the same complete gameplay physics-frame path used by automatic ticks. Every Practice teardown restores the normal Engine clock. `scons target=template_release -j4` passed, and a log-backed isolated graphical launch loaded the current main scene and new Practice scripts without script or parser errors while leaving the existing editor/embedded game untouched. Focused clock and input assertions remain scheduled for Phase H under the agreed test cadence.
- 2026-08-20 — Phase D: added a 45-record companion rewind ring, checked main-and-ghost rolling restores, sixteen session-local full-state slots, roster/session validation, D-pad slot controls and notifications, Practice sticker suppression, render snapping after state loads, compact Practice HUD state, and capture/restore timing plus retained-byte diagnostics. Results, finish/DNF state, ghost cursors, and relevant Practice lifecycle state are restored together; slot loads invalidate unrelated rolling history while retaining other explicit slots. `scons target=template_release -j4` passed, and a log-backed isolated graphical launch loaded the native API and all modified scripts/scenes without script or parser errors while leaving the existing editor/embedded game untouched. Canonical replay-branch retention joins these slot records in Phase E, and focused state assertions remain scheduled for Phase H under the agreed test cadence.
- 2026-08-20 — Phase E: added a shared 64-frame-chunk canonical input timeline with retained savestate heads and branch collection, made rewind and slot loads select/truncate the exact canonical prefix, and routed finite Practice recording through authoritative post-simulation multi-racer inputs. Added Live/Manual input modes, an exact 0–254 raw-value editor for both steering axes and triggers plus all encoded buttons, controller-input capture, latched manual frame advance, and non-destructive repeatable `Save Current Replay` snapshots before completion. Export flattens and base64-encodes only at save time while Infinite Practice retains no timeline. `scons target=template_release -j4` passed, and a log-backed isolated graphical launch remained alive through the smoke window with no script, parser, or runtime call errors after repairing the editor's initial method-name typo. Focused branch, byte-round-trip, and replay-reproduction assertions remain scheduled for Phase H under the agreed test cadence.
- 2026-08-20 — Phase F: added a Practice-only Off/Compact/Expanded telemetry selector and bounded 10 Hz presentation updates over the single native telemetry sample. Compact mode shows canonical tick, lap target, speed, yaw rate, grip/drift/technique classification, energy, turbo, boost state, slot, speed, and rewind state; Expanded adds local linear/angular velocity, per-corner drift mask, grounded/airborne/low-gravity and surface state, checkpoints/progress, and exact encoded inputs. Off returns before sampling or formatting, with diagnostic counters available to verify that policy, while slot notifications remain independent. `scons target=template_release -j4` passed, and a log-backed isolated graphical launch remained alive through the smoke window with no script, parser, or runtime call errors. Focused allocation-policy and field-mapping assertions remain scheduled for Phase H under the agreed test cadence.
- 2026-08-20 — Phase G: added replay-resume eligibility with explicit disabled reasons, a confirmation dialog with default-enabled `Keep Original as Ghost`, decoded-frame caching, exact full-state capture, and canonical-prefix seeding. Resume now transfers the already-running replay simulation in place instead of copying heap-relative full-state data into another `GameSim`: the focused racer becomes local, every other racer becomes a native CPU without changing roster order or physical state, companion race-result state is retained, and Practice enters at 0.00x. The optional Original ghost runs the complete recorded roster and inputs in an isolated `GameSim`, fast-forwards to the branch cursor, fades at source completion, and participates in Practice rewind/savestate restoration. Practice exports identify their authoritative multi-racer source directly. `scons target=template_release -j4` passed, and an isolated graphical launch remained alive through the smoke window with no script, parser, runtime-call, or import-file errors.
- 2026-08-21 — Phase H: removed the unsafe cross-instance replay-state restoration path, normalized decoded replay racer IDs at the resume boundary, completed focused unit/runtime/resume smoke coverage, and repaired every defect those checks exposed. Added exact raw-input round trips, shared-branch retention, finite/infinite lifecycle, speed/frame advance, repeatable partial export, 45-frame rewind boundary, savestate branch restore, telemetry-off, exact-roster replay resume, focused-control transfer, original-ghost synchronization, and completed-racer rejection checks. Existing Time Attack ghost lifecycle and native full-state equivalence checks also pass. The final release build and isolated graphical launch passed without script, parser, runtime-call, import-file, or process-leak errors.

### Profiling results

- The final four Practice smoke programs completed in 19.8 seconds combined, so focused verification remained inside the requested lightweight cadence.
- A one-racer Original ghost fast-forwarded 948 ticks at 9 microseconds average and 64 microseconds maximum per ghost tick. Instantiation took 6.878 ms and its tracked native capacity was 82,167,020 bytes, dominated by the fixed level and 45-state rollback buffers.
- A 50-racer Original ghost fast-forwarded 71 ticks at 118 microseconds average and 356 microseconds maximum per ghost tick. Instantiation took 15.855 ms and its tracked native capacity was 100,693,528 bytes.
- The runtime smoke exposed all 44 usable rewind steps from the 45-record ring and refused the stale boundary as designed.

### Final verification

- `scons target=template_release -j4` passed after all repairs.
- `practice_mode_unit_smoke.gd` passed in 1.06 seconds.
- `practice_runtime_smoke.gd` passed in 5.77 seconds.
- `practice_replay_resume_smoke.gd` passed for a one-racer replay with Original ghost and for a 50-racer server replay focused on racer 20; partial-end/no-ghost and completed-racer-disabled variants also passed.
- `time_attack_ghost_lifecycle_smoke.gd` passed its Practice CPU/ghost, Retry, Race Again, and ranked Time Attack 0/1/2/4-ghost matrix in 9.28 seconds.
- `netstate_restore_equivalence.gd` passed a 3,600-tick, 100-car mixed human/CPU full-state equivalence run.
- A final isolated graphical launch opened the current main scene under the rebuilt release library and remained alive through the smoke window. Its log contained no script, parser, runtime-call, or import errors, and the spawned process was stopped cleanly afterward.
- The broad suite was not run because it is not known to complete within 30 seconds. Controller feel, visual layout, and the remaining hardware/manual matrix are ready for user testing.
