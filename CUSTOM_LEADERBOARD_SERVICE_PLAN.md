# Custom Leaderboard Service Plan

## Status

Active implementation contract for the 0.3.2 leaderboard migration. The custom service replaces Steam Leaderboards as the authoritative Time Attack store. Steam remains the player identity provider and the one-time source for recoverable historical entries; new scores are not mirrored back to Steam.

## Objective

Store and serve verified Time Attack records with enough control to support:

- every valid completed ranked run and its exact replay, not only leaderboard-retained bests;
- each player's best time for every vehicle on every ranked track;
- an overall leaderboard derived from each player's fastest qualifying run;
- vehicle-filtered Global views, with Around Me and Friends as optional follow-up views;
- stable replay retrieval for leaderboard viewing, ghosts, audits, and moderation;
- exact track, vehicle, ruleset, machine-setting, replay, and Steam-account identity;
- moderation, deletion, migration, and audit operations controlled by MaxX Throttle;
- future categories and presentation changes without Steam-board proliferation.

The deterministic native replay verifier remains the only authority allowed to create ranked records.

## Current Constraints

The existing service already authenticates a Steam ticket, checks app ownership, runs the shipped game as a deterministic replay verifier, and obtains the verified track digest, vehicle ID, vehicle digest, machine setting, finish time, ruleset revision, replay schema, game version, and replay SHA-256. It currently sends that result to one Steam leaderboard and can lose the replay association when Steam accepts the score but a later attachment step fails.

Steam retains one score per user per leaderboard and does not provide the category, replay-retention, migration, or administrative control the game now needs. The new service must reuse the existing verification boundary rather than create a second verifier or trust client-provided result fields.

The 0.3.2 release is the authority cutover. Its leaderboard client targets the custom service. Steam leaderboard writes and reads are removed from the live submission path once the custom service has passed pre-release validation.

## Ranked Archive Scope

The game submits every completed ranked Time Attack run to the verifier, even when the client already knows that the time cannot improve a personal or leaderboard best. The client must never pre-filter a completed ranked run based on its time.

The permanent server archive includes every run that passes ranked verification using an official ranked track and an official vehicle. Whether the run changes a per-vehicle best, overall best, or any displayed leaderboard row has no effect on archival.

The archive explicitly excludes:

- unranked and Practice runs;
- runs using Practice-only settings;
- custom or loose tracks;
- custom, local-draft, or Workshop vehicles;
- corrupt replays and submissions that fail ranked verification.

Excluded runs remain governed by the game's existing local replay behavior and are never uploaded merely for analytics. Ranked eligibility and replay archival are decided from verifier output, not client claims.

## Steam Identity

Use the authenticated 64-bit Steam ID as the permanent player key. Persona names, avatars, and profile URLs are presentation metadata, never identity keys.

- Authenticate each game session or submission with a Steam authentication ticket.
- Resolve the ticket to a Steam ID and confirm ownership of the playtest or production app as appropriate.
- Store the Steam ID on every verified run and best row.
- Cache the latest persona name, avatar identifiers, and refresh timestamp for efficient board display.
- Refresh presentation metadata through Steam APIs without changing historical ownership.
- Preserve the display name captured at submission time for audit/history while showing the current cached persona name in ordinary leaderboard views.

The Steam Web API may not expose a private friends list. For the in-game Friends view, obtain the local player's friend Steam IDs from the signed-in Steam client, submit a bounded list of those IDs with the read request, and let the service filter and rank matching authoritative records. Client-provided friend IDs affect only which public leaderboard rows are displayed; they never affect score acceptance or ownership. A server-side Steam friends lookup may be used when available, but is not required for correctness.

## Competitive Identity

A competitive category is identified by:

- `track_gameplay_digest`;
- `ruleset_revision`;
- `vehicle_content_id` for presentation and stable lineage;
- `vehicle_gameplay_digest` for exact competitive behavior.

Machine setting is stored on every run and displayed, but does not split a vehicle category unless that policy is explicitly changed later.

Never silently combine different vehicle gameplay digests. If an official vehicle is rebalanced, expose the new digest as a new category revision or introduce an explicit season/revision that binds the complete eligible vehicle roster. Old data remains queryable but is not mixed into the current competitive view.

## Data Model

### `players`

One row per Steam account:

- Steam ID primary key;
- current cached persona name and avatar identifiers;
- presentation-metadata refresh timestamp;
- account moderation state and optional reason;
- first-seen and last-seen timestamps.

### `verified_runs`

Immutable verification and audit records:

- run ID primary key;
- authenticated Steam ID;
- replay SHA-256 with a uniqueness constraint scoped to the submitting Steam ID;
- replay object key, byte length, and storage state;
- track content ID and gameplay digest;
- vehicle content ID and gameplay digest;
- machine-setting percentage;
- verified time in milliseconds;
- ruleset revision, replay schema, and game version;
- captured submission-time persona name;
- submission and verification timestamps;
- provenance such as native submission or historical Steam import;
- moderation state and optional moderation reason;
- whether the run improved a per-vehicle and/or overall best at insertion time.

Repeated submission of the same authenticated Steam ID and replay digest is idempotent and returns the original run outcome.

### `player_vehicle_bests`

One current best for each:

```text
(track_gameplay_digest, ruleset_revision,
 vehicle_gameplay_digest, steam_id)
```

The row points to an immutable verified run. Replacement is an atomic conditional update that accepts only a lower time. Equal times retain the existing earliest record unless a different tie policy is deliberately chosen.

### Derived overall bests

The overall track board is derived by taking each player's lowest current per-vehicle best within the active competitive revision. It may be materialized if profiling shows the query is too expensive, but it must remain derivable from authoritative per-vehicle rows.

### Replay objects

Replay bodies do not belong in the relational database. Store every successfully verified completed ranked replay by SHA-256 in object storage. The digest is both the immutable object identity and an integrity check, and identical replay bytes may share one stored object.

Verified replays are retained indefinitely by default, including runs that do not improve any best. A run or object may be hidden by moderation, but ordinary cleanup must not destroy audit evidence. Any future retention or deletion policy requires an explicit migration and must preserve every object still referenced by a visible run, moderation hold, investigation, or historical import.

## Authoritative Submission Transaction

1. Receive an authenticated raw replay submission for every completed ranked run, without client-side best-time filtering.
2. Authenticate the Steam ticket and confirm app ownership.
3. Run the existing deterministic verifier against pinned track and vehicle content.
4. Ignore client claims for vehicle, setting, digests, and finish time; use only verifier output.
5. Compute the replay SHA-256 and apply idempotency checks.
6. Upload the exact verified replay bytes to object storage under their content digest.
7. Confirm the stored object's byte length and SHA-256.
8. In one authoritative database operation, insert the immutable verified run and conditionally improve the player's per-vehicle best.
9. Derive the player's overall best and returned ranks from custom authoritative data.
10. Return the run ID, retention result, per-vehicle result, overall result, ranks, and replay identity to the game.

There is no Steam leaderboard write in this transaction.

Object-first storage makes failure recoverable: an interrupted database finalization can leave an unreferenced content-addressed object that a later retry reuses or an offline maintenance job removes. A database row must never be presented as watchable until its object exists and hashes correctly. The client receives success only after both durable replay storage and authoritative metadata commit succeed.

## Read API

Provide versioned authenticated or public-read endpoints for the core leaderboard and replay workflow:

- track overall leaderboard;
- track leaderboard filtered to one vehicle category;
- current player's per-vehicle bests for a track;
- exact run and replay metadata;
- a short-lived replay download URL;
- available vehicle categories and their record holders for a track;
- optional run history for a player/category for audit or future UI use.

Around Me and Friends endpoints are desirable but do not block the initial authority cutover. Add them only after authoritative submission, Global reads, replay retention, replay retrieval, and operational recovery are reliable.

Reads require cursor-based pagination and deterministic ordering by time, then submission timestamp, then run identity. Do not emulate Steam's fixed top-100 limitation.

The game UI preserves Overall and Global presentation while adding a vehicle selector and concise "my best by vehicle" view. Around Me and Friends may be retained when practical, but may temporarily be hidden if their implementation would delay the reliable core service. Ghost selection consumes the same returned entries and replay identities. Persona and avatar failures degrade to a stable Steam-ID-based placeholder without making the leaderboard unavailable.

## Cloud Deployment

The intended durable layout is:

- Cloudflare D1 for players, immutable run metadata, current-best references, moderation state, and migrations;
- Cloudflare R2 for content-addressed replay bodies;
- a Cloudflare Worker API for reads, ranking, pagination, replay authorization, identity-metadata refresh, rate limiting, and private verified-run ingestion;
- the existing native verifier on the current trusted host for 0.3.2;
- later migration of the native verifier to a pinned Linux container or dedicated host without changing leaderboard semantics.

The native Godot verifier does not become Worker code. During the hybrid stage, it sends the exact replay plus a signed verification result to a private ingest endpoint. The Worker validates the verifier credential and payload contract, stores the replay object, and commits the authoritative metadata. It never accepts an untrusted client assertion as a ranked result.

Before implementation, confirm current Cloudflare limits and semantics for replay upload size, request duration, D1 transactions, and R2 uploads. If verified replay sizes make direct Worker ingestion unsuitable, use an authenticated staged R2 upload followed by verifier-signed finalization rather than weakening the trust boundary.

## Authentication and Abuse Controls

- Preserve Steam ticket authentication and app-ownership checks.
- Use a shared atomic TTL store for ticket replay prevention before running multiple verifier instances.
- Bind ingest requests to a rotating service credential inaccessible to clients.
- Rate-limit submissions by authenticated Steam ID and network source.
- Cap replay size and verification time before allocation or execution.
- Keep verifier concurrency bounded.
- Never log Steam tickets, service secrets, or raw authorization headers.
- Maintain soft-delete and quarantine moderation states rather than immediately destroying audit evidence.
- Keep admin and migration endpoints separate from public game endpoints.

## Historical Steam Import

Steam is used once as a migration source, not as an ongoing mirror:

1. Enumerate current retained Steam entries for every official board.
2. Download attached replays when available.
3. Hash and deterministically validate every recovered replay.
4. Insert the verified run, replay object, Steam identity, and vehicle category with Steam-import provenance.
5. Import score-only entries that cannot provide a replay only if they can be tied to a valid Steam account and board; mark replay availability explicitly as historical-unavailable.
6. Produce an audit report containing imported, unavailable, invalid, and conflicting entries.

Steam retained only each player's overall track best. Slower runs made with other vehicles that Steam rejected under `KeepBest` cannot be reconstructed globally unless a player still has the corresponding local replay and deliberately resubmits it. Existing entries without recoverable machine-setting details remain visibly unspecified rather than guessed.

After import and acceptance validation, the game no longer depends on Steam Leaderboards for normal reads, writes, ranks, replays, or ghosts.

## 0.3.2 Implementation Phases

### Phase A: Contract and local persistence

- Freeze category, tie, replay-retention, and identity semantics.
- Define D1 migrations and versioned request/response schemas.
- Add repository-local storage behind the real verifier completion path.
- Prove atomic per-vehicle `KeepBest`, idempotency, overall derivation, and permanent replay retention.

### Phase B: Cloud resources and API

- Provision development D1 and R2 resources and private ingest authentication.
- Implement verified-run ingestion, object-first replay storage, atomic best replacement, identity caching, and read endpoints.
- Add migrations, exports/backups, structured diagnostics, health checks, and administrative audit tools.

### Phase C: Pre-release shadow validation

- Send verified internal/playtest submissions to the custom service without changing the public 0.3.1 client.
- Compare custom overall ordering with recoverable Steam data.
- Exercise replay retrieval, vehicle filters, ghosts, idempotent retry, and deliberate failure recovery.
- Exercise Around Me and Friends if they are included in the 0.3.2 cutover; otherwise keep them out of the acceptance-critical UI.
- Repair discrepancies before authority changes.

### Phase D: Historical import

- Import every recoverable Steam entry and replay.
- Generate and review the migration audit report.
- Freeze the import snapshot immediately before 0.3.2 release and perform a final incremental import if required.

### Phase E: 0.3.2 authority cutover

- Make custom reads and writes the only in-game leaderboard path.
- Fetch leaderboard replays and ghosts from R2-backed storage.
- Remove Steam leaderboard write retries and Steam attachment dependencies from runtime behavior.
- Keep clear diagnostics and retry behavior for custom-service outages without silently falling back to Steam.

### Phase F: Verifier-host migration

- Produce and validate the Linux verifier build.
- Move verification from the developer PC to a pinned managed host/container.
- Exercise failure recovery and replay determinism across Windows and Linux before switching production traffic.

This phase is desirable but does not block 0.3.2 if the existing trusted Windows verifier is stable and monitored.

## Validation

- Concurrent submissions cannot replace a faster per-vehicle best with a slower one.
- Repeated identical submissions are idempotent and return the same run.
- Every successfully verified completed ranked run retains its exact replay even when it improves no best.
- A completed ranked run that is slower than the player's current best is still submitted, verified, and archived.
- Unranked, Practice, custom-track, and non-official-vehicle runs are not uploaded to the ranked archive.
- One player can retain independent bests for every eligible vehicle on one track.
- Overall rank uses only that player's fastest eligible vehicle result.
- Balance-revision digests never mix unintentionally.
- Every advertised replay downloads and matches its stored SHA-256.
- Failed object storage, database, verifier, and response delivery recover without duplicate or split-brain results.
- A lost response after commit is safely recoverable by resubmitting the same replay.
- Global reads, pagination, ties, moderation filtering, and ghost selection return deterministic results.
- Around Me and Friends return deterministic results when enabled, but do not block the initial service launch.
- Persona-name, avatar, and private-friends-list failures do not affect authoritative scores.
- Historical Steam-only entries degrade clearly when their replay or machine setting is unavailable.
- Load tests cover realistic board size, burst submissions, replay sizes, and four simultaneous ghost downloads.
- A complete backup/export restore reproduces leaderboard ordering and replay availability.

## Acceptance Criteria

- The custom store is authoritative for all verified Time Attack results.
- Every newly verified run has an immutable metadata record and verified replay object.
- Every valid completed ranked run is submitted and archived regardless of leaderboard improvement.
- Unranked and non-official-content runs remain outside the server archive.
- Each player has at most one current best per exact track/vehicle competitive category.
- Overall and vehicle-filtered boards are queryable without Steam board proliferation.
- SteamID64 provides durable ownership while names and avatars remain refreshable presentation data.
- Steam Leaderboards are not required for new submissions, reads, ranks, replays, or ghosts.
- Every recoverable historical Steam entry is imported with explicit provenance and replay availability.
- The service can be deployed, migrated, backed up, audited, moderated, and restored without developer-PC filesystem state being the only copy.
