# Steam Workshop, Custom Content, Car Editor, and Leaderboards Plan

Status: approved implementation contract

## Goal

Add direct Steamworks support to MaxX Throttle, define safe and portable packages for vehicles and tracks, build an in-game vehicle editor that can import a creator's glTF/GLB assets and author the existing `.mxt_car_props` data, provide a one-button test-drive loop, publish and install packages through Steam Workshop, and add useful Steam leaderboards without treating arbitrary client-authored content as trusted competition.

This plan deliberately does not include finishing the in-game track geometry editor. The existing track export remains the source of `track.mxt_track`; this work packages, validates, loads, shares, and eventually ranks those exports.

## Current Foundations

The project is not starting from zero:

- The release export already targets a Steam ContentBuilder tree, with separate main-game and Playtest app/depot scripts.
- The repository does not yet initialize or link the Steamworks API.
- `TrackContentController` already discovers external `.json` plus `.mxt_track` pairs, hashes `.mxt_track` bytes with SHA-256, and can load GLB/glTF visuals at runtime.
- Native race instantiation already accepts raw `.mxt_car_props` bytes. The property data therefore does not need to become a Godot resource.
- The Python car creator already defines the required stat and spline-authoring workflow.
- The garage already contains a reusable SubViewport, camera orbit, livery controls, and multi-pass car preview.
- The current car catalog and multiplayer/replay records identify cars by `res://.../definition.tres` paths. Those paths cannot identify Workshop content and must be replaced at their call sites.
- `CarRenderManager` currently depends on a trusted scene hierarchy containing `VEHICLE_MAIN`, outline/shadow passes, and optional `THRUSTERS`. A user-authored scene cannot safely be allowed to provide that hierarchy.

## Resolved Architecture

### 1. Integrate Steamworks directly into the existing GDExtension

Add a native `MxtSteamService` to `libgamesim` rather than adding a second Godot Steam plugin or an adapter layer.

The service will:

- initialize Steam once during application startup;
- run Steam callbacks from one well-defined frame hook;
- expose immutable account/app status and explicit asynchronous Workshop and leaderboard operations to Godot;
- own callback state and a small fixed-capacity request queue;
- shut down once during application exit;
- report Steam-unavailable as a normal runtime state so local development and non-Steam test runs still work.

Build integration will read the Steamworks SDK location from a SCons option or `STEAMWORKS_SDK_ROOT`; the SDK itself will not be copied into this repository. Windows exports will include the required redistributable Steam API DLL. `steam_appid.txt` is development-only and must not enter a shipped depot.

The initial integration targets Windows. Linux and macOS SDK libraries should be added when those exports become active, using the same service and direct platform-specific link settings.

### 2. Use one content-package system for both content types

A package is canonically a directory. This matches Steam Workshop, whose content payload is a folder. For manual sharing, the same directory can be encoded as a ZIP container with the `.mxtpkg` extension; extracting it must reproduce the canonical directory exactly.

Both package types use the same manifest and validator. The manifest's `content_type` selects the type-specific payload. There will not be separate car and track archive implementations.

Community packages are data-only. They may contain JSON, the existing MXT binary formats, GLB, explicitly supported image files, and a preview image. They may not contain:

- `.tscn` or `.scn` scenes;
- GDScript, C#, native libraries, shaders, or arbitrary Godot resources;
- absolute paths, remote URIs, symlinks, or paths escaping the package root;
- executable behavior embedded in another accepted format.

The editor accepts both `.gltf` and `.glb` source files. A `.gltf` import resolves only dependencies contained inside the selected import root, rejects absolute/remote/escaping references, and is normalized into a self-contained GLB before validation and packaging. Canonical packages therefore contain GLB regardless of the creator's source format.

### 3. Separate distribution identity from gameplay identity

Steam `PublishedFileId_t` identifies a mutable Workshop listing, not immutable gameplay. Three identities are needed:

- `published_file_id`: the Workshop listing and subscription handle, if any;
- `package_digest`: SHA-256 over every accepted file path, length, and byte sequence in canonical sorted order;
- `gameplay_digest`: SHA-256 over only the type-specific authoritative gameplay bytes plus a domain separator and the applicable gameplay/ruleset revision.

For a vehicle, the gameplay payload is the validated `.mxt_car_props` data. Its mesh, textures, preview, title, and description are presentation data.

For a track, the gameplay payload is the validated `.mxt_track` data. Its GLB, preview, and descriptive metadata are presentation data unless a future metadata field is explicitly promoted into race rules.

Changing a title or preview therefore changes the package digest but not leaderboard identity. Changing car physics or track collision produces a new gameplay digest. Hash input must be length-prefixed and domain-separated rather than formed by ambiguous byte concatenation.

### 4. Replace resource paths with content records

Create one catalog covering built-in, local-draft, local-installed, and Workshop-installed content. A resolved content record contains at least:

- content type and source;
- stable content ID;
- package and gameplay digests;
- Workshop item ID when applicable;
- display metadata;
- validated payload paths;
- loaded/cached visual data;
- validation and availability state.

Player settings, livery ownership, multiplayer race options, replay headers, car selection, and render-manager archetypes must use stable vehicle content IDs instead of `CarDefinition.resource_path`. Track selections must likewise use the catalog's track identity rather than an array position.

This is a direct migration: update the call sites and remove the obsolete path-as-identity fields. Do not retain a compatibility translation layer. Existing development assets become built-in catalog entries as part of the same change.

Workshop files remain read-only. Derived GLB resources or thumbnails are stored under a cache keyed by package digest, never written back into Steam's install directory.

### 5. Keep authoring native and the UI thin

The in-game editor should not port the Tkinter implementation line-for-line or create a second property codec in GDScript.

Add a native car-authoring session that owns:

- the editable car property model;
- stat names, units, legal ranges, warnings, spline keys, and sampling;
- `.mxt_car_props` parse, validation, and serialization through the same code that defines runtime validity;
- imported visual metadata, collision-corner data, and thruster transforms;
- dirty state and validation diagnostics;
- save, draft snapshot, and package-build operations.

Godot owns the actual editor screen, controls, graphs, gizmos, file dialogs, and preview viewport. Physics continues to consume prevalidated bytes with no editor abstractions in its hot path.

The Python editor can remain a developer tool during parity work. Once the in-game editor covers its useful workflows, either retain it explicitly as an internal batch tool or remove it; do not make shipped authoring depend on Python.

## Canonical Package Layout

### Shared manifest

Every package root contains `manifest.json` and `preview.png`:

```json
{
  "format_revision": 1,
  "content_type": "vehicle",
  "title": "Example Machine",
  "description": "Creator-authored description",
  "author_name": "Example Creator",
  "payload": {
    "model": "vehicle/model.glb",
    "properties": "vehicle/properties.mxt_car_props"
  },
  "payload_sha256": {
    "vehicle/model.glb": "...",
    "vehicle/properties.mxt_car_props": "...",
    "preview.png": "..."
  }
}
```

The package builder writes paths in normalized UTF-8 form, rejects duplicate case-folded paths, and writes hashes for all payload files. `package_digest` is calculated outside the manifest so it is not circular.

`format_revision` exists because Workshop packages are persistent external data. During current development an incompatible revision may be rejected outright; no migration layer is required.

### Vehicle package

```text
manifest.json
preview.png
vehicle/
  model.glb
  properties.mxt_car_props
  visual.json
```

Textures should normally be embedded in `model.glb`. If independent paint-mask or normal-map slots prove necessary, add them as explicit manifest fields rather than permitting arbitrary files to influence rendering.

The manifest/editor also owns constrained presentation metadata that is not naturally stored in the property blob: model transform, selected mesh/surface roles, paint channels, and thruster transforms. Store this in an explicit `vehicle/visual.json` once the exact editor controls are settled; do not encode it as a Godot scene.

The public vehicle MVP supports:

- one static vehicle body, with selected mesh surfaces where needed;
- albedo, normal, and paint-mask inputs supported by MaxX Throttle's own materials;
- scale, rotation, and origin adjustment;
- the existing four tilt/suspension and four wall-collision corners;
- a bounded number of thruster positions and orientations;
- no skeletons, animations, lights, cameras, particles, custom shaders, scripts, or user-provided audio.

The game creates its own main, shadow, outline, and stamp passes from the validated mesh. `CarRenderManager` should consume a resolved visual record directly instead of searching a user scene for magic node names.

### Track package

```text
manifest.json
preview.png
track/
  track.mxt_track
  visual.glb
  metadata.json
```

`track.mxt_track` remains authoritative for collision and race geometry. `visual.glb` is presentation. `metadata.json` contains the currently supported environment/display fields; every field must be parsed explicitly and bounded.

The first track-package tool is an assembler/publisher around an existing exported `.mxt_track`, not a replacement track editor. It lets a creator choose the export, visual GLB, metadata, and preview, then validates and builds the package.

Trusted built-in tracks may continue using internal project resources. Community packages must never select `.tscn`/`.scn`, even though the current trusted development loader can load them.

## Validation and Content Safety

All imported or downloaded content goes through the same native validation path before it enters the catalog. Validation occurs during an explicit import/install transition, not during race simulation.

The validator must:

1. Parse the manifest with strict required fields, recognized keys, types, and bounded string lengths.
2. Normalize all relative paths and reject traversal, absolute paths, remote URIs, symlinks, duplicate paths, device names, and alternate path spellings.
3. Enforce archive entry count, compressed size, uncompressed size, per-file size, and compression-ratio budgets before extraction.
4. Verify declared SHA-256 values and calculate package/gameplay digests.
5. Parse `.mxt_car_props` or `.mxt_track` with their authoritative native validators.
6. Inspect GLB structure before generating a scene: bounded nodes, meshes, primitives, vertices, indices, materials, image dimensions, total texture pixels, and hierarchy depth.
7. Reject unsupported extensions and unsupported GLB features instead of silently ignoring them.
8. Build MaxX Throttle-owned materials and runtime objects from validated data.
9. Cache the successful result by package digest and store diagnostics for the UI.

Initial budgets should be named constants and profiled before public release. A reasonable first pass is a 64 MiB vehicle package, a 512 MiB track package, 4096-pixel texture dimensions, a total texture-pixel budget, a 250k-triangle vehicle budget, and a 2M-triangle track budget. These numbers are starting limits, not a promise; loading-time and VRAM measurements should determine the public values.

Workshop tags should include the content type and package format revision. The compact Workshop metadata field should include content type, format revision, and gameplay digest so listings can be filtered before download; the downloaded manifest remains authoritative.

## In-Game Car Editor Workflow

### Main flow

1. Open **Garage -> Car Creator**.
2. Create an empty draft, duplicate an owned/local car, or reopen a draft.
3. Import `model.glb` into a private draft directory under `user://`.
4. Resolve model warnings and choose the vehicle mesh/surfaces.
5. Adjust scale, rotation, origin, material inputs, paint-mask behavior, and thrusters in the existing orbiting preview.
6. Position collision corners with viewport gizmos and numeric fields.
7. Edit machine-setting splines and special-state tables using the concepts from `mxt_car_creator.py`.
8. Inspect sampled values, validation errors, warnings, expected speed behavior, and resource budgets.
9. Press **Test Drive** to race an immutable snapshot immediately.
10. Return to the exact draft and editor tab after leaving the race.
11. Save locally, build a portable `.mxtpkg`, or publish/update a Workshop item.

### Stat editor

The stat interface should provide:

- searchable categories rather than one enormous form;
- a graph with direct key manipulation plus precise numeric fields;
- base, MTS, quickturn, boost-state, and S-BOOST views where applicable;
- machine-setting sampling and the existing speed preview without an analog-throttle option;
- reset/copy/paste operations at the stat or table level;
- hard errors for invalid serialization/runtime state and separate warnings for dangerous but legal values.

Undo/redo belongs to the editor session and should use bounded snapshots or compact edit commands. It must not allocate or retain history in the race runtime.

### Test drive

Test drive uses the real single-player race creation and native simulation. It must not have a separate simplified physics scene.

Pressing the button will:

- validate the current draft;
- write an immutable temporary package snapshot with stable digests;
- register that snapshot in the content catalog;
- save an editor-return token containing draft ID, selected tab, camera state, and scroll/selection state;
- open the normal track picker, defaulting to the last test track;
- start a standard one-player race using the snapshot's vehicle content ID;
- mark the session local/custom and therefore leaderboard-ineligible;
- return to the car editor when the player exits the test drive.

Edits made after the snapshot do not mutate the active race. Pressing Test Drive again creates a new snapshot only when the draft digest has changed.

## Steam Workshop Flow

The native Steam service follows the normal ISteamUGC sequence:

- create a Workshop item for the main game's consumer app;
- stage the validated canonical package directory;
- set title, description, tags, preview, compact metadata, and content directory;
- submit the update and display upload/progress state;
- surface the Workshop legal-agreement requirement and open the item page when necessary;
- query subscribed items at startup and when Steam reports subscription/install changes;
- resolve each installed folder, validate it, and then add it to the catalog;
- allow explicit unsubscribe/open-page/update actions without altering the installed folder directly.

Use the main MaxX Throttle app as the Workshop consumer. Configure the Playtest app with publish permissions to that Workshop for development rather than creating a second incompatible content ecosystem. Start with hidden or friends-only items until install/update behavior and moderation tools are proven.

Workshop is the distributor, not the multiplayer file-transfer protocol. Multiplayer advertises exact content IDs, package/gameplay digests, and Workshop IDs. A peer that lacks a required package downloads it through Steam, validates it, and only then becomes race-ready. Unpublished drafts are single-player test-drive content. Do not transmit arbitrary package bytes over gameplay networking in the first implementation.

## Multiplayer and Eligibility Policy

Initial policy:

- Official tracks plus official car physics: eligible for the matching official leaderboard ruleset.
- Custom visuals paired with official physics: defer until the identity model explicitly supports that split.
- Any custom vehicle physics: unranked.
- Any uncurated custom track: unranked on Steam leaderboards.
- Multiplayer with custom content: allowed after every peer validates the exact required package; still unranked.
- Missing, invalid, changed, or hash-mismatched content prevents race readiness with a useful diagnostic.

The host sends exact content records as part of race admission. Every peer acknowledges the same gameplay digests before loading. This extends the current staged race-admission process rather than downloading in the middle of simulation.

Replay headers must store content IDs, gameplay digests, ruleset revision, and package/Workshop references. A replay is playable only when its exact gameplay payloads are available and validated. Paths and Workshop IDs alone are insufficient.

## Steam Leaderboards

### Official-track MVP

Start with exactly one Time Attack leaderboard for each official track. Every official vehicle competes on the same track board. Define each board in a checked-in leaderboard manifest and create/configure it in Steamworks before shipping.

Each board is identified by an immutable tuple:

```text
(track_gameplay_digest, time_attack_ruleset_revision)
```

Use a readable Steam name containing a track slug, short digest, and Time Attack ruleset revision. Upload finish time in integer milliseconds, set the board to ascending order, and use `KeepBest`. The current simulation tick result remains the source; conversion and rounding must be centralized so local display, replay verification, and submission agree.

Time Attack has one forced ruleset rather than exposing the general single-player race toggles:

- bumpers are disabled;
- S-BOOST is disabled;
- vehicle restore is enabled;
- the board manifest fixes the lap count and every other score-affecting race option.

These values must come from an authoritative Time Attack options constructor and be checked again by replay verification. Merely preselecting or disabling the existing UI checkboxes would not be an adequate rules boundary. A later Practice mode can expose bumpers, S-BOOST, vehicle restore, and other single-player experimentation without producing leaderboard submissions.

Leaderboard details can carry a compact fixed schema containing the ruleset/build revision, car gameplay identifier, lap/checkpoint facts, assist flags, and replay format revision. Steam's detail array is limited, so it is not a replacement for a replay.

Submission eligibility is decided by one `LeaderboardEligibility` result produced at race start and finalized at race end. It rejects:

- custom or hash-mismatched gameplay content;
- unsupported mode, lap count, or race options;
- debug/cheat state;
- invalid finish state or discontinuous timing;
- replay recording failure;
- any future rule explicitly excluded by the board manifest.

The UI downloads global, around-user, and friends ranges and clearly labels offline/pending upload state.

### Replay verification and ghost attachment

Every score submission includes the completed replay for deterministic verification before the leaderboard entry is written. Sharing that verified replay through Steam Remote Storage/UGC and attaching its handle to the leaderboard entry may follow as a presentation feature; downloaded entries can then offer **Race Ghost** or **Watch Replay** after validating the replay header and required content.

This requires migrating replay car/track references to content IDs first. Replays remain untrusted input: they must pass the same bounds checks and deterministic re-simulation before being treated as evidence.

### Trust model

There are no public client-writable leaderboards. Configure each board as trusted and submit through a small verification service using Steam's Web API from the first leaderboard release. The service verifies Steam identity and ownership, validates the exact board/content/ruleset tuple, re-simulates the uploaded replay or authoritative input stream, and submits the verified score. The leaderboard feature is not complete until this service exists.

Do not automatically create one native Steam leaderboard per Workshop item. Workshop listings are mutable and their IDs do not identify a geometry revision. Community competition should eventually use one of these policies:

- only curated Workshop gameplay digests receive a configured Steam leaderboard; or
- a separate MaxX Throttle leaderboard service stores records by exact gameplay digest and ruleset revision.

The recommended first public policy is Steam boards for official tracks, then curated community track revisions after replay verification exists.

## Implementation Phases

### Phase 0 - Product and Steamworks setup

- Confirm Workshop consumer app, Playtest publish permissions, legal text, visibility defaults, tags, quotas, and moderation roles in Steamworks Partner Site.
- Record the fixed Time Attack ruleset: one board per official track, all official vehicles, bumpers off, S-BOOST off, and vehicle restore on.
- Freeze `manifest.json` revision 1 and the digest algorithms in a short format specification.
- Choose initial content budgets after measuring representative built-in cars and tracks.

Exit gate: a private test Workshop item can be created manually for the main app, and the package/leaderboard policy decisions below are accepted.

### Phase 1 - Native Steam bootstrap

- Add Steamworks include/link configuration to `SConstruct`.
- Add and register `MxtSteamService` in the existing GDExtension.
- Initialize, pump callbacks, shut down, expose account/app status, and handle Steam-unavailable development runs.
- Copy the redistributable API library into the export and confirm main-app and Playtest startup.

Exit gate: a release build made with `scons target=template_release -j4` reports the expected Steam App ID and user while launched through Steam, and starts cleanly without Steam during local development.

### Phase 2 - Content manifest, validation, and catalog

- Add native manifest parsing, path normalization, hashing, content-type validation, and catalog records.
- Add local package-directory and `.mxtpkg` import/export.
- Migrate built-in tracks and vehicles into catalog entries.
- Replace car resource paths and track indices in settings, liveries, race setup, multiplayer, and replay metadata with content IDs.
- Refactor vehicle rendering to consume validated visual records instead of user-authored scene hierarchy.
- Keep external/community track visuals GLB-only.

Exit gate: built-in and manually installed local packages select, render, race, replay, and round-trip through settings by stable content ID.

### Phase 3 - Local car-editor MVP

- Add native car-authoring session and expose the authoritative property schema/codec.
- Build the Godot car-editor screen using the existing preview and garage visual language.
- Implement glTF/GLB import with canonical GLB normalization, surface/material setup, transforms, collision-corner and thruster gizmos.
- Implement stat categories, spline graphs, S-BOOST/modifier editing, sampling, speed preview, diagnostics, and local saving.
- Generate a validated local vehicle package without Steam.

Exit gate: a player can begin with a glTF or GLB asset, author every supported property, close/reopen the draft, and select the resulting vehicle in the garage.

### Phase 4 - Instant test drive

- Add immutable draft snapshots to the content catalog.
- Route Test Drive through the standard single-player race path and selected track.
- Preserve editor state across the race and return to the draft on exit.
- Mark draft/custom sessions ineligible for leaderboard submission.

Exit gate: changing a stat, pressing Test Drive, racing with the exact new snapshot, exiting, editing again, and retesting is a short repeatable loop with no editor restart or Steam upload.

### Phase 5 - Workshop vehicle publishing and installation

- Implement CreateItem, update staging, metadata/tags/preview, SubmitItemUpdate, progress, agreement handling, and item-page navigation.
- Discover subscribed/installed items and react to install/update callbacks.
- Validate and cache installed vehicle packages before catalog registration.
- Display missing, downloading, invalid, outdated-format, and ready states in the garage and lobby.

Exit gate: two Steam accounts can publish, subscribe, update, install, select, and use the same exact vehicle package in an unranked multiplayer race.

### Phase 6 - Track packaging and Workshop support

- Add the in-game package assembler for an existing `.mxt_track`, GLB, metadata, and preview.
- Apply the same import/publish/install/catalog pipeline to track packages.
- Extend multiplayer admission for exact track and vehicle package sets.
- Keep full track geometry editing deferred.

Exit gate: an exported custom track can be packaged, published, subscribed to, selected, and raced by all peers without copying files manually.

### Phase 7 - Trusted official Steam leaderboards

- Add the checked-in leaderboard manifest and native async leaderboard request queue.
- Add the authoritative Time Attack options constructor with bumpers off, S-BOOST off, vehicle restore on, and no user-overridable score-affecting options.
- Define the replay verification service and authenticated submission protocol.
- Re-simulate every submission against its exact official content digests and Time Attack ruleset.
- Configure the Steam boards as trusted and submit verified scores through the Steam Web API.
- Implement eligibility, score conversion, KeepBest upload, range downloads, and UI.
- Add pending/offline behavior without blocking race results.
- Migrate replay identity fully and retain verified replays for later shared replay/ghost attachment.

Exit gate: eligible official Time Attack replays deterministically verify, upload through the trusted service, and display consistently; custom, debug, modified-rule, and mismatched sessions are rejected with an explicit reason. No public leaderboard accepts a direct client write.

### Phase 8 - Curated community boards

- Add a curation workflow that binds an approved Workshop track revision to its immutable gameplay digest.
- Decide whether broader community boards belong in Steam or a dedicated service.

Exit gate: an approved community track revision has one trusted board using the same forced Time Attack rules, and updating its Workshop listing cannot alter the geometry underlying that board.

## Likely Code Organization

Exact filenames may change during implementation, but responsibilities should remain separated:

```text
src/mxt_core/steam_service.{h,cpp}
src/content/content_manifest.{h,cpp}
src/content/content_catalog.{h,cpp}
src/content/content_validator.{h,cpp}
src/content/content_package.{h,cpp}
src/car/car_authoring_session.{h,cpp}
src/track/track_package_validator.{h,cpp}
mxto/ui/car_editor.tscn
mxto/ui/car_editor.gd
mxto/ui/content_browser.tscn
mxto/ui/content_browser.gd
mxto/steam/leaderboards.json
docs/mxt_content_package_format.md
```

Native files own parsing, hashing, validation, Steam callback state, property serialization, and bounded content data. GDScript owns menus, editor interaction, previews, transitions, and user-facing diagnostics. Race physics keeps its current allocation-free sampled data path.

## Verification Strategy

Verification should be proportional rather than adding a test for every UI edit.

The important automated boundaries are:

- package manifest/path/archive rejection cases;
- package and gameplay digest determinism;
- `.mxt_car_props` native read/write round-trip and invalid-data rejection;
- malicious/oversized GLB and archive budgets;
- content-ID serialization through settings, multiplayer admission, and replay headers;
- deterministic replay re-simulation before trusted leaderboard work.

Each implementation phase also gets one end-to-end smoke path using the release build. Workshop and leaderboard phases require real two-account/Steam-client tests because their callback, permission, legal-agreement, and install behavior cannot be proven by local mocks alone.

## Resolved Product Defaults

1. **Ranking:** one Time Attack board per official track, with every official vehicle competing together; curated community track revisions may receive boards later.
2. **Time Attack rules:** bumpers off, S-BOOST off, vehicle restore on, with all score-affecting options fixed by the ruleset.
3. **Practice mode:** later single-player mode for freely configuring bumpers, S-BOOST, vehicle restore, and other experimental options without leaderboard submission.
4. **Vehicle model scope:** accept static glTF/GLB imports, normalize them to GLB, and use MaxX Throttle-owned materials, collision corners, and thrusters; no animation or custom audio initially.
5. **Workshop visibility:** hidden/friends-only during development, public after validation/moderation tooling is exercised.
6. **Package sharing:** one `.mxtpkg` ZIP container for manual import plus the identical unpacked folder for Workshop.
7. **Trust:** no public client-written boards; replay re-simulation and trusted Web API submission are required for the first leaderboard release.
8. **Community track boards:** curated exact gameplay digests rather than one board for every mutable Workshop listing.
