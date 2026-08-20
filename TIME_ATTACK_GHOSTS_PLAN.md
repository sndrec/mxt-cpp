# Time Attack Leaderboard Ghosts Plan

## Status

Implementation in progress. Phase A is complete in code; integrated runtime verification remains in Phase F.

This document is the source of truth for adding selectable leaderboard ghosts to Time Attack. Execute it with a short goal such as:

> Follow the plan in `TIME_ATTACK_GHOSTS_PLAN.md`.

Keep this document updated as work is completed. Record material deviations and the reason for them instead of silently changing the design.

## Objective

Allow a player starting a Time Attack run to select zero to four verified leaderboard times and race against them as non-interactive ghosts.

Each ghost must reproduce its retained leaderboard replay in an independent `GameSim`. A ghost must never share mutable simulation state with the player's race or another ghost. This isolation includes mines, triggers, restore state, checkpoints, race events, and any future mutable track state.

Ghosts are available in both Ranked Time Attack and Practice Unranked. Selecting ghosts must not make an otherwise eligible Ranked run unranked.

## Locked Product Decisions

- The initial limit is four selected ghosts.
- Zero ghosts remains the default and behaves exactly like current Time Attack.
- The player's own retained leaderboard time is selectable.
- Entries may be selected from Global Top 100, Around Me, and Friends views.
- Only entries with an attached, trusted replay can be selected.
- Every ghost uses a separate single-car `GameSim`.
- Ghosts do not collide with or otherwise affect the player or one another because they do not inhabit the same simulation.
- Ghost simulations use the replay's exact vehicle, machine setting, input frames, start-grid data, and spawn seed.
- Ghost selection is local presentation state. It is not submitted to Steam, included in trusted leaderboard details, or added to the player's replay roster.
- Ranked verification continues to see exactly one racer and the canonical Time Attack options.
- Selected ghosts survive Retry and Race Again.
- Selection remains available while returning to the Time Attack setup for the same track during the current game session. It is not persisted across game launches in the first version.
- Older compatible-schema leaderboard replays remain selectable. The picker identifies them with the existing older-version/desync warning.
- A ghost fades out after its replay ends.
- Ghosts have no vehicle audio, world audio, camera, HUD, physical shadow, damage interaction, placement participation, or player-facing race events.
- Initial presentation is a translucent, tinted version of the recorded machine with a stable per-slot color and player name. Presentation constants can be tuned after visual review.
- No leaderboard backend, Steam leaderboard schema, or replay schema change is required.

## Explicit Non-Goals

- Online multiplayer ghosts.
- Ghost collisions or attacks.
- More than four ghosts before profiling supports raising the limit.
- Live delta-time calculation, racing line analysis, or coaching overlays.
- A global ghost library outside Time Attack setup.
- Persisting ghost selections across game launches.
- Uploading a second replay or embedding selected ghost replays into the player's submitted replay.
- Reproducing ghost-generated mine explosions, sparks, damage sounds, or other world effects in the player's visible world.
- Changing leaderboard scoring, verification rules, or retained-score behavior.

## Existing Systems to Preserve

- `mxto/ui/time_attack_setup.gd` and `.tscn` own the pre-race Ranked/Practice decision.
- `mxto/steam/leaderboard_client.gd` requests Global, Around Me, and Friends entries.
- `mxto/steam/leaderboard_details.gd` authenticates and decodes trusted Steam detail words.
- `mxto/steam/leaderboard_replay_service.gd` currently attaches submitted replays and downloads a single replay for playback.
- `mxto/steam/leaderboard_replay_validator.gd` validates downloaded leaderboard replay content.
- `mxto/replay/replay_controller.gd` owns normal replay recording and playback.
- `mxto/core/race_session_controller.gd` configures the player's normal race simulation.
- `mxto/vehicle/car_render_manager.gd` already supports manually submitted vehicle transforms.
- `mxto/core/session_memory_telemetry.gd` records simulation and rendering memory.
- `GameSim` already exposes single-player ticking, memory statistics, player transforms, and independent track state when separately instantiated.

Do not force ghost support through the normal replay-playback mode. A ghost run is still a live Time Attack run whose main simulation records the local player normally.

## Target Data Flow

1. Time Attack setup requests leaderboard entries for the selected track.
2. The player checks up to four replay-bearing entries in the ghost picker.
3. Each selection is queued through a serialized replay cache/download service.
4. Cached and downloaded bytes are SHA-256 checked and passed through the existing trusted replay validation.
5. The picker marks each selection Ready, Downloading, Warning, or Failed.
6. Ranked and Practice start buttons are enabled only when every selected ghost is Ready.
7. The setup passes immutable prepared ghost descriptors to the Time Attack session without putting them in canonical race options.
8. Race startup creates one independent, single-car `GameSim` for each descriptor.
9. The live Time Attack tick is the master clock. At tick N, each active ghost simulation consumes replay input frame N.
10. A shared ghost renderer displays the interpolated transforms produced by the independent simulations.
11. Retry destroys and recreates the four slots from tick zero using the already cached replay data.
12. Race exit destroys every ghost simulation and clears transient decoded frame data.

## Domain Records

Use explicit dictionaries or small typed data classes with the following logical records. Do not pass raw leaderboard rows throughout unrelated systems.

### Ghost selection

- Board name
- Steam ID
- Persona name
- Global rank
- Score in milliseconds
- UGC handle
- Trusted replay SHA-256
- Trusted detail dictionary
- Stable display color slot

The replay SHA-256 is the stable selection identity. Rank is display data and may change after refresh.

### Prepared ghost replay

- Selection identity and display fields
- Validated cache path
- Parsed replay metadata
- Single racer ID
- Exact player settings
- Vehicle content ID and gameplay digest
- Machine setting
- Replay spawn seed
- Start-grid slot
- Input frames or a transition-time decoded input representation
- Frame count and finish tick
- Compatibility-warning state

Prepared records must be immutable for the duration of a race.

### Runtime ghost slot

- Slot index and color
- Independent `GameSim`
- Replay racer ID
- Prepared input data
- Current replay frame index
- Active/finished/fading state
- Render transform history
- Setup, tick, render, and memory counters

Allocate at race transition, reuse through the race, and free on race exit. Do not allocate per simulation tick.

## 1. Leaderboard Ghost Picker

Add a modal or contained overlay launched by a `Choose Ghosts (N/4)` button on the Time Attack setup screen. Do not make the existing setup panel grow beyond the viewport.

The picker contains:

- Global Top 100, Around Me, and Friends view buttons.
- Refresh.
- A scrollable tree with checkbox, rank, player, machine and setting, replay/game version, time, and readiness.
- Selected count and the four-ghost limit.
- Retry Download for failed selections.
- Clear Selection.
- Done/Back.

Behavior:

- Reuse the selected Time Attack board; the picker cannot change tracks.
- Preserve checked entries while switching leaderboard views.
- Include the local player's entry like any other selectable entry.
- Disable rows whose UGC handle or trusted replay digest is missing.
- Enforce the four-entry limit at toggle time and explain the limit in the status line.
- Begin cache validation/download immediately when a row is checked.
- Cancel the consumer request when a row is unchecked. An in-flight Steam download may finish and warm the cache, but it must not reselect the row.
- Disable Ranked and Practice while any checked entry is unresolved.
- Do not silently drop a failed ghost and start with fewer than the player selected. Require Retry or deselection.
- Show the older-version warning on the affected row without preventing selection.
- Give each selected row the same color later used for its ghost and name label.
- Support mouse, keyboard, and controller navigation. The normal accept action toggles the focused checkbox.

Extract genuinely shared leaderboard-entry decoration if needed so the full leaderboard browser and picker agree about vehicle name, machine setting, version, and replay availability. Do not introduce a forwarding wrapper around either UI.

## 2. Replay Download and Cache Queue

The Steam native service currently permits one active replay download. Keep that native constraint and serialize ghost downloads in GDScript.

Separate replay download/cache responsibility from replay-attachment upload responsibility cleanly:

- The existing attachment queue continues to upload and attach the player's retained replay.
- A replay cache component owns trusted replay download requests, cache hits, validation, cancellation tokens, and completion signals.
- Update the full leaderboard browser's Watch Replay path to consume the same cache component directly.
- Remove replaced download logic from the old owner rather than retaining a compatibility forwarding layer.

Cache rules:

- Keep `user://leaderboard_replays` as the cache root.
- Key cache files by trusted replay SHA-256, not rank or persona.
- Validate cached bytes on every first use in a game session.
- Delete and redownload a cached file that fails digest or trusted metadata validation.
- Deduplicate concurrent requests for the same digest.
- Serialize actual Steam downloads.
- Retain the existing maximum replay byte limit.
- Release raw download buffers and JSON text after validation/preparation.
- Return explicit failure reasons suitable for the picker.
- Never add cached leaderboard replays to the user's saved local replay catalog.

Validation must continue to check replay digest, board, ruleset revision, replay schema, track gameplay digest, vehicle gameplay digest, and machine setting when present in trusted details.

## 3. Ghost Simulation Controller

Add a focused Time Attack ghost controller rather than expanding `main.gd` with slot management.

Suggested location:

- `mxto/time_attack/time_attack_ghost_controller.gd`

Responsibilities:

- Own a fixed pool of four runtime slots.
- Prepare selected replays before race start.
- Create and configure one `GameSim` per active slot.
- Advance ghosts from the live Time Attack tick.
- Collect transforms for rendering.
- Track finish/fade state.
- Expose aggregate profiling and memory statistics.
- Destroy all native and Godot state deterministically on Retry, menu return, track change, and application shutdown.

The controller must not own leaderboard UI, normal replay playback, trusted score submission, or the player's main `GameSim`.

### Simulation initialization

For each selected replay:

- Use the same track bytes as the live run, while giving the ghost simulation its own parsed track and mutable state.
- Instantiate exactly one car using replay settings and the exact official vehicle properties.
- Apply replay machine setting, racer ID, CPU=false, start-grid slot, and spawn seed.
- Apply canonical Time Attack simulation options: restore enabled, bumpers disabled, S-BOOST disabled, and no CPU racers.
- Disable multiplayer intro-camera behavior.
- Do not bind the main car container, main render manager, spatial audio manager, spark container, gameplay camera, network manager, or race-results owner.
- Start the ghost simulation on the same logical tick as the player's simulation.

If a lightweight render-snapshot update requires a Node3D container, provide only the minimal empty container needed by `GameSim`; do not instantiate four normal race presentation stacks.

### Tick behavior

- The live `_singleplayer_tick` is authoritative.
- Validate frame numbering during preparation so the runtime path does not search dictionaries or recover malformed streams.
- Predecode input data at the race transition. Per-tick base64 decoding and per-tick container allocation are prohibited.
- Tick each active ghost once for each live player tick.
- Feed neutral input only if the replay has legitimately reached its terminal state; malformed or missing in-range input is a preparation failure.
- Never consume a ghost simulation's race events into the player's race presentation or results.
- Never call the player's finish, DNF, placement, replay-recording, leaderboard, audio, or notification paths for a ghost.
- Stop ticking after the prepared replay ends and enter the fade state.
- Pausing the live race naturally pauses ghosts because ghost ticks occur only from the live Time Attack tick path.

### Input representation

Decode the one-racer leaderboard replay into a transition-time representation optimized for sequential access. Prefer one contiguous `PackedByteArray` plus fixed frame stride or another allocation-free tick representation. If a small generic native `GameSim` entry point is required to consume an indexed frame without slicing, add it directly and document why. Do not add a wrapper that copies into the existing call every tick.

## 4. Ghost Rendering

Use one shared `CarRenderManager` configured manually for all active ghost definitions and liveries. The four physics simulations remain separate; only their render submissions are combined.

Rendering behavior:

- Gather an interpolated render transform from each ghost simulation.
- Submit at most four manual vehicle instances per render frame.
- Retain the recorded vehicle mesh, machine livery, and normal vehicle proportions.
- Apply stable slot colors, initially cyan, magenta, yellow, and green.
- Apply a tunable translucent ghost treatment.
- Disable physical shadow casting for ghost render passes.
- Render a small player-name label in the matching slot color.
- Do not create race HUDs, cameras, audio listeners, engine/boost audio, or ordinary nametags.
- Thruster visuals may be shown if they can be driven without binding the full effect/audio stack. They must use the ghost treatment and no lights. If the first presentation is visually noisy, disable them rather than affecting physics work.
- Do not render ghost-induced mines, sparks, collisions, damage, pickups, or other mutable world state.
- Fade a ghost out over a short tunable interval after its replay ends, then stop submitting it.

Use per-manager duplicated materials or `GeometryInstance3D` transparency so the normal vehicle shaders and main race materials do not move into the transparent render pipeline. Presentation changes must remain isolated to the ghost renderer.

## 5. Time Attack and Replay Integration

Thread the selected prepared descriptors through transient Time Attack session state, not `race_options`.

Required behavior:

- Ranked eligibility evaluation receives unchanged canonical options.
- `RaceSessionController.start_race` continues to build only the player's normal simulation roster.
- Normal replay recording captures only the player input and one-racer roster.
- Leaderboard verifier input stays byte-for-byte compatible with the existing replay schema and canonical roster expectations.
- Retry destroys and reconstructs ghost simulations before tick zero.
- Race Again retains the selected descriptors and cached replays.
- Returning to the Time Attack setup on the same track retains selection for the session.
- Changing track clears selections and cancels their consumer requests.
- A leaderboard refresh updates display ranks but retains selection by replay digest.
- Watching a replay remains separate from racing against it as a ghost.
- Practice runs with CPU racers may also use ghosts; CPUs remain in the player's main simulation and ghosts remain isolated.

Do not add selected ghost metadata to the trusted replay or leaderboard detail words. If local diagnostics need to identify selected ghosts, keep that information in logs or transient controller state.

## 6. Instrumentation

Collect inexpensive counters continuously and emit summaries at meaningful transitions. Do not print per tick.

### Setup and memory

Record:

- Selected and ready ghost count.
- Cache-hit and downloaded byte totals.
- Replay parse/predecode time per slot and total.
- `GameSim` instantiation time per slot and total.
- Each ghost simulation's tracked native bytes.
- Level heap, game-state heap, and rollback/history bytes per slot and aggregate.
- Ghost render-manager archetype and instance counts.

Extend session memory telemetry with ghost count and aggregate ghost simulation memory. Sample before preparation, after all slots are ready, at race start, and after teardown.

### Runtime timing

Record:

- Ghost ticks executed.
- Per-slot tick total and maximum microseconds.
- Aggregate ghost tick total, average, and maximum microseconds.
- Ghost render submission total, average, and maximum microseconds.
- Active and fading counts.
- Main live simulation frame timing alongside ghost timing so the incremental cost is visible.

Emit one concise `MXT_GHOST_PROFILE` summary at race completion or exit. Use the existing profiling/debug logging conventions for more detailed output.

Profile 0, 1, 2, and 4 ghosts on the same track and machine before considering a higher limit. The four-ghost limit remains until measurements on representative complex tracks show acceptable CPU time, frame pacing, and memory.

## 7. File Organization

Expected primary files:

- `mxto/ui/time_attack_setup.gd`
- `mxto/ui/time_attack_setup.tscn`
- New `mxto/ui/time_attack_ghost_picker.gd`
- New `mxto/ui/time_attack_ghost_picker.tscn`
- `mxto/steam/leaderboard_client.gd`
- `mxto/steam/leaderboard_replay_service.gd`
- New `mxto/steam/leaderboard_replay_cache.gd`
- `mxto/steam/leaderboard_replay_validator.gd`
- New `mxto/time_attack/time_attack_ghost_controller.gd`
- `mxto/main.gd`
- `mxto/core/race_session_controller.gd` only where lifecycle hooks are genuinely required
- `mxto/vehicle/car_render_manager.gd`
- `mxto/core/session_memory_telemetry.gd`
- `src/gamesim/gamesim.h`, `src/gamesim/gamesim_core.cpp`, or a focused implementation file only if an allocation-free indexed-input or render-snapshot capability is required

Keep UI, Steam cache transport, ghost orchestration, rendering, and native simulation concerns separate. Do not grow `main.gd`, `replay_controller.gd`, or `leaderboard_replay_service.gd` into a second monolith.

## 8. Implementation Sequence

### Phase A: Cache and selection model

- [x] Extract the reusable replay cache/download queue.
- [x] Rewire Watch Verified Replay to the new cache owner without a forwarding shim.
- [x] Add request deduplication, cancellation tokens, cache validation, and explicit errors.
- [x] Add the four-entry selection model keyed by replay digest.

Completion gate: one to four replay requests can be selected, downloaded sequentially, validated, cached, canceled, and retrieved without starting a race.

### Phase B: Ghost picker UI

- [x] Add the Time Attack `Choose Ghosts (N/4)` control.
- [x] Add the modal picker with Global, Around Me, Friends, checkboxes, readiness, warnings, Retry, Clear, and Done.
- [x] Include the player's own time.
- [x] Enforce four selections.
- [x] Complete keyboard/controller focus and accept behavior.
- [x] Prevent race start while selected ghosts are unresolved.

Completion gate: a player can reliably prepare any zero-to-four replay-bearing entries without overflowing the setup screen.

### Phase C: Independent simulation slots

- [x] Add the fixed four-slot ghost controller.
- [x] Prepare validated replay input into an allocation-free sequential representation.
- [x] Instantiate one single-car `GameSim` per selection with independent track state.
- [x] Advance slots from the live Time Attack tick.
- [x] Handle terminal, fade, Retry, Race Again, track change, and teardown.
- [x] Confirm ghost events never enter the player's result or presentation systems.

Completion gate: four invisible ghosts can reproduce four input streams while the player's ranked simulation and replay remain canonical and single-racer.

### Phase D: Presentation

- [x] Add the shared manual ghost render manager.
- [x] Render independent simulation transforms with four stable colors.
- [x] Add translucency, no-shadow behavior, player labels, and end-of-replay fading.
- [x] Evaluate optional lightweight thruster presentation without audio or lights; keep it disabled for the initial uncluttered treatment.
- [ ] Tune visibility against dark, bright, enclosed, and high-speed tracks.

Completion gate: ghosts are readable and distinguishable without obscuring the track or looking like ordinary collidable racers.

### Phase E: Instrumentation and cleanup

- [ ] Add setup, tick, render, and memory counters.
- [ ] Extend session memory telemetry.
- [ ] Emit the concise race summary.
- [ ] Profile 0, 1, 2, and 4 ghosts.
- [ ] Remove temporary diagnostics and dead experimental paths.
- [ ] Update this document with measured results and any justified deviations.

Completion gate: logs make the incremental cost of each ghost count obvious enough to evaluate a future limit increase.

### Phase F: Final validation and repair

- [ ] Run all focused automated checks after implementation is complete.
- [ ] Fix discovered problems, then rerun the affected checks.
- [ ] Complete representative manual Time Attack runs.
- [ ] Record final verification results below.

## 9. Verification Policy

During implementation, keep iteration fast:

- Compile the release target after native changes with `scons target=template_release -j4`.
- Launch the game and confirm it opens after coherent integration slices.
- Do not launch a second Godot editor against the live project while the user's editor is open.
- Use only narrowly relevant smoke checks during implementation when they complete in under 30 seconds.
- Do not repeatedly run the full test suite while building the feature.

After all implementation work is complete, run focused and broader validation:

### Automated checks

- Zero-ghost Time Attack remains unchanged.
- Replay-cache hit, miss, deduplication, cancellation, invalid digest, missing UGC, and retry behavior.
- Picker selection across all three views, including the local player's entry and the four-entry limit.
- Four independent simulations receive the correct tick-indexed input.
- Retry reconstructs every slot at tick zero.
- A mutable trigger changed in one simulation does not change it in the player or another ghost simulation.
- Ghost finish and fade do not finish or block the player's race.
- Ranked replay output still has one racer, one input per frame, canonical options, and passes the trusted replay validator.
- Practice with CPUs and ghosts keeps CPU and ghost systems separate.
- Cache files do not appear in the local saved replay catalog.
- Teardown leaves no `GameSim`, renderer, node, or cached decoded-frame leaks.

### Manual matrix

- Ranked with 0, 1, 2, and 4 ghosts.
- Practice with ghosts and with CPUs plus ghosts.
- Select own best, a friend, and Global entries.
- Cached and first-download startup.
- Missing attachment and failed download handling.
- Older-version warning entry.
- Retry from pause and Race Again from results.
- Ghost finishes before the player; player finishes before the ghost.
- Ghost restore/fallout and mine contact.
- Flat road, closed cylinder, pipe/open cylinder, mesh road, and a trigger-heavy track.
- Multiple vehicle shapes, liveries, and machine settings.
- Keyboard/mouse and controller-only selection.

## 10. Acceptance Criteria

The feature is complete when:

- A player can select any zero-to-four replay-bearing leaderboard entries, including their own.
- All selected replays are trusted, cached, and ready before the run starts.
- Each ghost follows its replay through an independent simulation with independent mutable level state.
- Ghosts never affect the player's physics, track state, results, ranking, replay, or eligibility.
- Ranked submissions produced while ghosts are active still verify and attach normally.
- Retry and Race Again preserve and restart ghosts correctly.
- The UI fits at supported resolutions and is fully controller navigable.
- Ghosts are clearly distinguishable and non-obstructive.
- Four-ghost CPU, frame-time, and memory costs are captured in useful logs.
- Release compilation, launch verification, focused checks, final tests, and the manual matrix have been completed.
- No dead paths, forwarding shims, temporary compatibility layers, or per-tick allocation regressions remain.

## Execution Record

### Material deviations

- None yet.

### Completed slices

- Phase A: reusable serialized leaderboard replay cache, direct Watch Replay integration, and the four-entry digest-keyed selection model.
- Phase B: fixed-size leaderboard ghost picker, shared leaderboard row decoration, own-time selection, readiness gating, and controller navigation.
- Phase C1: transition-time input packing, allocation-free indexed native input consumption, isolated one-car simulations, live-tick advancement, and deterministic teardown.
- Phase D1: shared manual rendering, unique transparent archetypes, stable slot tinting, no shadows/effects, projected names, and terminal fade-out.

### Profiling results

- Not yet measured.

### Final verification

- Not yet run.
