# Loading, Replay, and Content Pipeline Modernization Plan

## Status

In progress. Phase A is complete; Phase B is next. This document is the implementation
contract for the loading-performance, binary-replay, lobby-rendering, and
Workshop-refresh work described below.

Execute it with a short goal such as:

> Follow the plan in `LOADING_REPLAY_AND_CONTENT_PIPELINE_PLAN.md`.

Keep this document updated while implementing it. Record measurements, completed
phases, and material design changes in the document instead of silently changing
the contract.

## Objectives

- Remove test-drive snapshot validation from game startup and installed-content
  refreshes.
- Avoid repeatedly validating the same immutable content bytes during one content
  transaction.
- Replace JSON/base64 gameplay replays with a compact native binary format.
- Make replay catalog enumeration proportional to replay count and metadata size,
  not total recorded frame data.
- Stop unchanged lobby cars from resampling physics when another player changes
  settings.
- Replace whole-catalog Workshop refreshes with added/changed/removed item deltas.
- Profile and optimize lobby vehicle renderer construction before deciding which
  remaining work should run asynchronously.
- Keep the current completed lobby render visible while replacement content is
  prepared, then swap atomically.

## Locked Decisions

### Test-drive snapshots

- Historical test-drive snapshots are not an installed-content library.
- Do not scan or validate them at startup.
- Do not scan or validate them during an ordinary installed-content refresh.
- Test Drive creates an immutable snapshot for the requested draft and registers
  only that snapshot.
- A successful validation result must be carried through the transaction; the same
  immutable directory must not be independently revalidated by export, import,
  catalog registration, and race startup.
- Imported and Workshop packages remain fully validated before use.
- Obsolete test-drive snapshots may be removed at an explicit safe transition once
  no active test drive references them. Never perform broad startup deletion.

### Replay format

- Replace `.replay.json` gameplay replays with a new compact binary format.
- The shipped game writes only the new binary format.
- Retain a narrowly scoped read-only legacy JSON reader for replay attachments
  obtained through the trusted Steam leaderboard replay service.
- Do not expose the legacy reader to the local replay browser, arbitrary file-open
  paths, new recordings, Practice exports, or multiplayer host saves.
- Retiring this leaderboard-only reader belongs to a separate future plan and is
  not part of this plan's completion criteria.
- Recording storage and binary serialization belong in native C++, not an Array of
  per-frame GDScript Dictionaries.
- Replay metadata is stored at the front of the file and can be read without
  reading, allocating, parsing, or decompressing frame data.
- Racer inputs are stored in stable roster-slot order. Do not repeat player IDs or
  textual keys every tick.
- Frame data is divided into independently compressed, indexed blocks so playback,
  verification, ghosts, practice continuation, and seeking do not require one
  monolithic parse.
- Historical leaderboard conversion is outside this plan. The runtime
  exception is limited to watching or using ghosts from trusted legacy Steam
  leaderboard attachments; it must not become a general compatibility layer.
- Game, verifier, submission service, leaderboard replay storage, replay browser,
  ghosts, multiplayer host recording, local saving, and Practice/TAS continuation
  cut over together.

### Lobby updates

- A global lobby-settings revision may trigger a roster walk, but each car's player
  revision must be checked before definition lookup or machine-stat sampling.
- An unchanged car performs no content resolution, stat sampling, mesh rebuild, or
  material rebuild.
- Content refresh invalidation is identified by exact content ID and content digest.
- A changed Workshop item must not invalidate unrelated Workshop definitions or
  lobby renderer archetypes.

### Lobby rendering

- First profile and optimize synchronous renderer construction. Do not use a worker
  thread to conceal unexplained 1.5-second construction work.
- Preserve the existing atlas-based production stamp renderer. Do not add a second
  non-atlased lobby-only stamp renderer.
- Once synchronous work is understood and reduced, remaining CPU-only preparation
  may run asynchronously.
- Godot rendering-resource creation and renderer/material mutation remain on the
  main thread unless the current engine API explicitly guarantees otherwise.
- The old completed render remains visible while a replacement is prepared.
- Rapid changes are coalesced. Obsolete builds are discarded by generation rather
  than installed and immediately replaced.
- Stamp-atlas threading is deferred until profiling shows it is worthwhile. The
  measured atlas build is not currently the dominant cost.

### Existing Steam leaderboard boundary

- This plan does not replace Steam Leaderboards, change leaderboard identity, or
  migrate historical rows.
- New replay submissions use the binary format after its coordinated cutover.
- Existing trusted Steam leaderboard attachments may remain in legacy JSON and stay
  watchable/selectable as ghosts through the dedicated read-only legacy boundary.
- Separate future work owns historical migration and eventual removal of the legacy
  reader.

## Explicit Non-Goals

- Leaderboard backend replacement, historical migration, or deployment.
- Changing per-vehicle, overall-best, ranking, pagination, or retention semantics.
- Removing the trusted leaderboard-only legacy JSON reader.

## Baseline Measurements

The initial instrumentation pass measured the following on the current development
machine and generated replay corpus:

- Game startup: approximately 4.6 seconds.
- Historical test-drive snapshot scan: approximately 4.0 seconds.
- Valid test-drive snapshot validation: approximately 0.92-1.06 seconds each.
- Replay catalog metadata scan, 44 files / approximately 114 MB: 0.53-0.56 seconds.
- Full JSON parsing of the same replay corpus: approximately 7.35 seconds.
- Initial 48-player stamped lobby renderer construction: approximately 1.53 seconds.
- Shared 48-player custom-stamp atlas construction: approximately 23-27 ms.
- One changed archetype in a 48-player stamped lobby: approximately 50 ms.
- Full atlas rebuild during that change: approximately 27 ms.
- Redundant definition/stat preparation for 48 cars: approximately 21 ms.

The replay benchmark corpus contains ten Twist Road replays at each of 6, 15, 30,
and 100 racers. Preserve the generator and use equivalent corpora for before/after
measurements.

## Phase A: Preserve and deepen measurements

Before changing a slow path, make its elapsed time attributable.

### Snapshot validation instrumentation

Record separate durations and bytes for:

- manifest read and parse;
- directory enumeration and structural validation;
- declared payload SHA-256 verification;
- payload validation by type;
- GLB parse and structural inspection;
- car-properties deserialize/sample;
- authoring-intent materialization and comparison;
- package digest generation;
- gameplay digest generation;
- catalog-record construction and publication.

Count file opens and total bytes read per validation. The instrumentation must make
duplicate reads visible rather than reporting only one aggregate duration.

### Renderer construction instrumentation

For each new or rebuilt archetype, record:

- definition lookup and GLB load;
- GLTF document parse;
- scene generation;
- mesh traversal and surface extraction;
- base vehicle mesh/material preparation;
- custom stamp projection mesh generation;
- native stamp mesh upload;
- material duplication and parameter assignment;
- outline/trail/thruster preparation;
- renderer archetype registration;
- RenderingServer resource creation/upload where measurable;
- number of unique definitions, liveries, stamp layers, vertices, indices, surfaces,
  materials, images, and textures processed.

Profile initial 48-player construction, one native-vehicle change, one Workshop
vehicle change, one livery-only change, and an installed Workshop item update while
the lobby remains open.

### Workshop refresh instrumentation

Record item-level validation cache hits, validation duration, registration duration,
definition load duration, invalidated content IDs, invalidated lobby players,
invalidated archetypes, and renderer rebuild duration.

### Acceptance

- At least 90% of the measured snapshot and renderer wall time is attributed to
  named stages or explicitly identified engine calls.
- Instrumentation has bounded output and does not log per vertex, per pixel, or per
  simulation tick.

### Phase A results

- Completed by the Phase A instrumentation commit.
- Four valid historical test-drive snapshots took 0.94-1.03 seconds each. Authoring
  intent materialization/comparison consumed 0.89-0.91 seconds per package, while
  GLB validation consumed 0.02-0.10 seconds. Each validation opened 25 files and
  logically read 4.4-7.1 MB across repeated validation/hash passes.
- The 48-player, 16-stamps-per-car lobby profile took 1.575 seconds inside archetype
  configuration. Per-archetype attribution identified native stamp projection and
  generated-mesh upload as approximately 95% or more of every expensive archetype.
- A one-player stamped archetype change took 50.9 ms for renderer configuration:
  19.9 ms built the one replacement All Rounder stamp mesh, while the remaining
  archetypes were reused. The surrounding full shared-atlas preparation took 23.6
  ms.
- An ordinary one-player settings change still spent 22.3 ms walking and sampling
  all 48 cars, confirming the Phase C early-out target.
- Profiling output is one bounded record per validation, content refresh, renderer
  configuration, or Workshop item—not per vertex, pixel, or simulation tick.

## Phase B: Remove test-drive startup work

- Remove `_scan_test_drive_snapshot_library()` from vehicle-content startup.
- Remove it from ordinary installed-content refresh.
- Confirm no garage, editor, replay, multiplayer, or test-drive UI enumerates this
  directory as selectable installed content.
- Replace the current export/import/register chain with one native snapshot
  transaction that:
  1. materializes or copies the current immutable package;
  2. validates it once;
  3. derives package and gameplay digests;
  4. places it at its content-addressed destination;
  5. registers the already-validated record;
  6. returns the record needed to start Test Drive.
- If the digest destination already exists from the current session, reuse its
  validated record. Do not validate it once to decide whether it is reusable and
  then validate it again to register it.
- Retain full validation diagnostics on the Test Drive action.

### Validator optimization

- Stream each declared file once for declared SHA-256, package digest, and gameplay
  digest updates where those hashes consume the same bytes.
- Reuse bounded payload bytes already read for validation instead of reopening small
  files.
- Do not weaken path, size, hash, schema, GLB, texture, audio, properties, or
  authoring-intent validation for external packages.
- Keep immutable validated-package data native through registration; do not turn it
  into a lossy Dictionary and then reconstruct or revalidate it.

### Acceptance

- Historical test-drive snapshot count and size do not affect startup duration.
- Startup performs zero test-drive snapshot validations.
- One Test Drive request validates the produced immutable package no more than once.
- Corrupt imported or Workshop packages remain rejected.

### Phase B results

- Completed by the Phase B snapshot-pipeline commit.
- Vehicle-content startup and installed-content refresh no longer enumerate or
  validate the historical Test Drive snapshot directory. On the same development
  machine used for Phase A, the measured headless `game_manager_ready` transition
  fell from 4.64 seconds to 0.619 seconds; no Test Drive snapshot transition or
  validation appeared in the startup profile.
- Test Drive now writes the mutable authoring package without validating it, then
  performs one native snapshot transaction that copies only manifest-declared files,
  validates the immutable staging package once, installs it by package digest, and
  registers the native validated record directly. The archive export/import and
  second catalog-validation chain is gone.
- A digest already registered during the current process reuses its catalog record
  without validating the destination again. A pre-existing destination without a
  current-process validated record is displaced before the newly validated snapshot
  is installed.
- Package validation now streams each file once for its declared SHA-256, package
  digest, and authoritative gameplay digest. Bounded properties and JSON payloads
  retained from that pass are reused by schema and authoring validation.
- The focused content-package smoke covers corrupt hash rejection, archive
  validation, snapshot registration, and same-session snapshot reuse.

## Phase C: Per-player lobby revision early-out

- Compare the car's applied player-settings revision with the incoming player's
  revision before `get_definition()` and machine-stat sampling.
- Preserve local-control ownership updates even when gameplay/render settings are
  unchanged.
- Treat explicit content invalidation as a separate reason to rebuild; do not fake a
  player revision change.
- Preserve roster add/remove behavior and magnifier selection.

### Acceptance

- Changing one player's machine in a 48-player unchanged-content lobby samples stats
  for exactly one player.
- Unchanged cars retain their renderer archetypes and materials.
- The focused scale smoke reports no redundant definition/stat work.

### Phase C results

- Completed by the Phase C lobby-revision commit.
- The controller now compares each car's applied settings revision before definition
  lookup or machine-stat sampling. An unchanged car performs only the cheap
  local-control ownership update.
- Local-player ownership has its own applied ID and update reason. Explicit vehicle
  content refresh records the current global settings revision after its forced work
  instead of assigning a fake stale revision and repeating the entire pass next frame.
- The 48-player scale smoke changed one machine and measured exactly one definition
  lookup/stat sample, 47 revision early-outs, 47 reused renderer archetypes, and one
  replacement archetype. Scheduling the change fell from 24.4 ms in Phase A to 3.8
  ms; the remaining 78.2 ms deferred render rebuild is owned by later renderer phases.

## Phase D: Incremental Workshop catalog updates

Replace clear-and-repopulate refreshes with an item delta.

- Maintain catalog state keyed by published file ID with install identity, package
  digest, gameplay digest, content ID, root path, and validated-package result.
- Diff the incoming Steam item set into added, changed, removed, and unchanged IDs.
- Validate each added or changed installed item once.
- Register the already-validated native package record without a second validation.
- Remove only records belonging to removed or invalidated IDs.
- Replace only records belonging to changed IDs.
- Rebuild only affected vehicle definitions.
- Preserve cached definitions, lobby-ready packages, atlas regions, and renderer
  archetypes for unchanged IDs.
- Emit a content delta containing exact added/changed/removed content IDs instead of
  forcing every consumer to infer changes from a global notification.
- Lobby consumers reconfigure only players whose selected content ID is in that
  delta.
- Coalesce repeated Steam install/update callbacks for the same final item state.

### Acceptance

- A no-op Workshop refresh validates and registers zero packages.
- Updating one item validates it once and reloads one definition.
- Unrelated Workshop cars remain visible and retain their render resources.
- Installing or updating an item while a lobby is open cannot create a repeated
  download/rebuild loop.
- Removal or invalidation swaps affected players to the existing safe native vehicle
  policy without disturbing other players.

### Phase D results

- Workshop package state now lives in the native catalog and is retained by published
  file ID, stable install identity, validated package, digests, record, and errors.
  Transient Steam state bits such as downloading/update-pending no longer invalidate
  that identity or trigger another package walk.
- Each added or materially changed install is validated once. The resulting native
  `ValidatedPackage` is registered directly, removing the former GDScript validation
  followed by a second native validation.
- Catalog mutation is an exact added/changed/removed item and content delta. Only
  affected Workshop definitions are reparsed, and unchanged definition objects remain
  live.
- Workshop deltas no longer run the full track/content consumer refresh. Lobby content
  refresh targets only players selecting an affected content ID and retains completed
  renderer managers so their unchanged archetypes can be reused.
- The focused content-package smoke observed one validation on first installation,
  then one native cache hit, zero validation, and zero catalog mutation when only
  transient Steam state changed. Exact removal emitted one removed content ID.
- The 48-player lobby smoke still sampled one changed player and early-outed the other
  47; the changed-player render rebuild remained approximately 76.4 ms and is the
  measured cost owned by Phases E and F.
- Release compilation, the focused content-package smoke, the 48-player lobby smoke,
  and a headless main-scene launch completed successfully.

## Phase E: Optimize renderer construction synchronously

Use the Phase A profiles to remove root costs before adding concurrency.

Candidate optimizations must be justified by measurements, but likely areas include:

- cache parsed immutable GLTF/mesh data by exact package digest;
- cache base vehicle archetype geometry independently from livery/stamp data;
- avoid regenerating a Godot scene when native immutable mesh data is already cached;
- reuse material and shader variants keyed by actual immutable inputs;
- rebuild only the custom-stamp projection/material portion for livery-only changes;
- cache native stamp projection geometry when vehicle mesh and stamp transform are
  unchanged;
- eliminate repeated mesh traversal, ArrayMesh surface extraction, image decoding,
  and texture upload within one content generation;
- retain renderer archetypes by exact render signature across lobby reorder and
  unrelated player changes;
- batch unavoidable RenderingServer uploads at the transition boundary.

Do not cache mutable nodes or resources under an incomplete key. Package digest,
vehicle definition revision, livery/stamp digest, and relevant render settings must
be part of the identity.

### Performance gate

Do not proceed to threading merely because work remains. First:

- account for at least 90% of renderer construction time;
- remove duplicate parsing, traversal, projection, and upload;
- demonstrate that cached archetype reuse is effectively constant-time;
- reduce the measured 48-player initial construction by at least 4x, or document the
  specific engine/resource cost that prevents it;
- attempt to bring one-player changed-archetype main-thread work within one 60 Hz
  frame before accepting it as asynchronous work.

If a target cannot be met, record the measured reason and proceed based on evidence,
not optimism.

## Phase F: Non-blocking lobby replacement builds

After Phase E:

- Split renderer preparation into CPU-only immutable build data and main-thread
  rendering-resource installation.
- Run only thread-safe CPU preparation on workers.
- Key requests by lobby/content/render generation.
- Coalesce repeated changes and retain only the newest pending generation.
- Keep the old completed vehicle visible while preparation runs.
- Install completed resources atomically during a bounded main-thread handoff.
- Discard obsolete results without touching the active renderer.
- Bound worker count and memory. Lobby churn must not launch one unbounded job per
  callback or player.
- Record queue wait, worker duration, discarded builds, handoff duration, and peak
  temporary memory.

### Stamp atlas follow-up

- Initially retain synchronous atlas construction because it is not the dominant
  cost.
- If later profiles justify it, port the existing garage worker pattern to the
  lobby: build the `Image` on a worker and create/apply `ImageTexture` on the main
  thread.
- Prefer stable per-player atlas regions and dirty-region updates. Repack the full
  atlas only when the roster/region layout requires it.
- Keep the prior atlas active until the replacement atlas and dependent archetypes
  are ready for the same generation.

### Acceptance

- Workshop installation and ordinary vehicle changes no longer freeze the lobby for
  multi-frame CPU construction.
- Players never flash to a blue fallback solely because their replacement renderer
  is still building.
- Obsolete queued work cannot overwrite newer content.
- Race startup receives a complete, matching content/render generation.

## Phase G: Native binary replay container

### Container contract

Define an explicitly little-endian format with:

- fixed magic and format version;
- fixed-size preamble containing bounded section offsets and lengths;
- checksummed metadata and frame sections;
- bounded counts and sizes validated before allocation;
- metadata section containing race identity, exact content evidence, ruleset/game
  compatibility, settings, results, replay name, and timestamps;
- roster table assigning each racer one stable frame slot;
- indexed compressed frame blocks;
- optional indexed event/checkpoint sections only when required by existing replay
  features.

Use an established fast compression library already suitable for distribution, or
add one deliberately after measuring representative replay blocks. Compression must
be independently blockable and bounded; do not wrap the entire file in one stream.

### Native recording

- Replace per-tick GDScript Dictionaries and duplicated `PackedByteArray` objects
  with a native append-only recording buffer.
- Store inputs in roster order using the existing compact input packet encoding.
- Allocate block storage at race transition and grow only in bounded chunks.
- Expose metadata and playback access needed by GDScript UI without exposing every
  frame as Variants.
- Save incrementally or from completed native blocks without base64 conversion.
- Preserve canonical Practice timeline branching through native block/range
  ownership rather than materializing an Array of frame Dictionaries.

### Native reading

- Metadata-only open reads only the preamble and metadata section.
- Full playback validates section bounds/checksums and lazily decompresses blocks.
- Ghost simulations can read independent cursors from shared immutable decoded
  blocks.
- Replay seeking uses the block index and existing simulation checkpoints.
- Verifier input reads the same canonical parser as the game.
- Rename edits rewrite only metadata or use a fixed/replaceable metadata section;
  they must not parse and rewrite all frame data.

### Runtime cutover

- Introduce a new extension such as `.mxt_replay`.
- Update local save/watch, multiplayer host autosave, staged Time Attack replay,
  leaderboard submission, leaderboard download/cache, ghosts, replay catalog,
  strict verifier, debug tooling where appropriate, and Practice continuation.
- Bump replay format/schema and the relevant Time Attack ruleset identity.
- Remove the general runtime `.replay.json` reader and the JSON writer in the same
  completed phase.
- Route trusted leaderboard attachments through a separate, read-only legacy decode
  boundary. Validate attachment identity, schema, counts, sizes, and decoded input
  bounds before allocation or playback.
- Old local JSON files may remain on disk but are not listed as playable current
  replays and cannot reach the leaderboard-only decode boundary.

### Acceptance

- Catalog metadata work reads a bounded amount per file independent of racer count
  and duration.
- The 44-file benchmark catalog opens in under 100 ms on the development machine
  after ordinary filesystem warm-up.
- Full load and save are materially faster than JSON; record wall time, peak memory,
  bytes read/written, and resulting size for 6, 15, 30, and 100-racer samples.
- Recording performs no per-tick Dictionary construction and no base64 conversion.
- Corrupt lengths, counts, offsets, checksums, compressed blocks, and truncated files
  fail safely before unsafe allocation or playback.
- A binary replay produces the same deterministic verification result as its source
  input stream.

## Verification Strategy

During implementation, keep feedback proportional:

- Build with `scons target=template_release -j4` after native slices.
- Launch the game after each coherent phase and verify that it opens.
- Run only focused checks that complete quickly while implementation is active.
- Do not repeatedly run the full suite during every phase.
- After all implementation phases are complete, run the relevant full automated
  suite, replay determinism matrix, content validation matrix, lobby scale smoke,
  Workshop update scenarios, leaderboard replay regressions, and final interactive
  checks. Fix failures at that point.

Performance comparisons must use the same machine, build target, content corpus,
roster, and instrumentation mode. Record medians over multiple runs and retain raw
profiles when results determine architecture.

## Commit and Pause Discipline

- Preserve unrelated dirty work.
- Commit each completed, release-built, launch-checked phase independently.
- Do not mix backend deployment, binary format, content validation, and renderer
  construction changes in one commit.
- Stop at a clean phase boundary if a later phase cannot be completed coherently.
- Update this document's status and measurements as phases land.

## Final Acceptance Criteria

- Startup does not inspect historical test-drive snapshots.
- Test Drive validates one immutable snapshot once.
- Content validation avoids redundant whole-file reads without weakening external
  package security or correctness.
- One lobby player's settings change performs no definition/stat work for unchanged
  players.
- One Workshop item change validates, registers, reloads, and rerenders only affected
  content and players.
- Lobby renderer construction has a complete profile, measured synchronous
  optimizations, and a non-blocking replacement path for remaining expensive work.
- Replay catalog enumeration is metadata-bounded and comfortably handles the
  generated 40-replay stress corpus.
- Replay recording, saving, loading, verification, ghosts, and Practice continuation
  use the native binary container.
- New local, multiplayer, Practice, submission, and authoritative leaderboard replay
  paths use only the binary format.
- Legacy JSON playback is possible only for trusted Steam leaderboard attachments;
  ordinary local replay paths cannot invoke it.
- Existing Steam leaderboard behavior otherwise remains unchanged by this plan.
