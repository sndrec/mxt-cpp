# MaxX Throttle Leaderboard API

This Cloudflare Worker is the authoritative metadata and replay store for ranked Time Attack. The native verifier remains responsible for Steam authentication, app ownership, official-content validation, and deterministic replay simulation. Only its signed output can enter this service.

## Storage

- D1 stores Steam identities, board identities, immutable verified runs, and conditional per-vehicle best references.
- R2 stores every successfully verified completed ranked replay by SHA-256.
- Slower runs remain in `verified_runs` and R2 even when `player_vehicle_bests` does not change.
- Practice, unranked, custom-track, and non-official-vehicle runs never reach the ingest endpoint.

The ingest operation stores and checksum-validates the replay object before committing its D1 metadata. Retrying the same authenticated Steam ID and replay digest is idempotent.

## Local development

1. Copy `.dev.vars.example` to `.dev.vars` and replace the development secrets.
2. Install dependencies with `npm install`.
3. Generate bindings with `npm run types`.
4. Run `npm run check` and `npm test`.
5. Run `npx wrangler d1 migrations apply mxt-leaderboards-development --local` before interactive `npx wrangler dev` use.

The integration tests use the current Cloudflare Vitest plugin and real local D1/R2 bindings.

## Cloud resources

`wrangler.jsonc` initially contains a zero D1 identifier so local type generation and tests have a complete binding contract. Before deploying a real environment:

1. Create separate development and production D1 databases and R2 buckets.
2. Replace the placeholder database ID and bind the intended bucket.
3. Run `scripts/provision_secrets.ps1 -Environment staging` (and then `production`) to create or reuse the untracked `INGEST_SECRET`, `REPLAY_URL_SECRET`, `MIGRATION_SECRET`, and `ADMIN_SECRET` values and install them with Wrangler. Use `-Rotate` only for an intentional credential rotation.
4. Apply D1 migrations remotely.
5. Run `npm run check`, `npm test`, `npm run deploy:dry`, and `npx wrangler check startup`.
6. Deploy and verify `/healthz` before configuring the native verifier or game client.

The verifier and Worker must share `INGEST_SECRET`. `REPLAY_URL_SECRET` belongs only to the Worker.

Apply migrations before deploying Worker code that depends on them:

```powershell
npm run migrate:staging
npm run deploy:staging
npm run migrate:production
npm run deploy:production
```

Verify each environment's `/healthz` after deployment. Keep staging and production secrets, D1 databases, and R2 buckets separate.

## API

- `POST /v1/ingest/verified-run` — private HMAC-authenticated verifier ingest with the replay as its body.
- `POST /v1/admin/import-historical-score` — migration-secret-authenticated import for explicitly replay-unavailable Steam history.
- `POST /v1/admin/moderation` — admin-secret-authenticated soft moderation for runs, historical scores, and players.
- `GET /v1/leaderboards/{board_id}` — Global or Around Me board, optionally filtered by vehicle gameplay digest.
- `GET /v1/boards/{board_id}/categories` — available vehicle categories.
- `GET /v1/boards/{board_id}/players/{steam_id}/bests` — one player's per-vehicle bests.
- `GET /v1/runs/{run_id}` — exact visible run metadata, including explicit historical replay availability.
- `GET /v1/runs/{run_id}/replay-url` — short-lived replay URL authorization.
- `GET /v1/replays/{run_id}` — signed replay download.
- `GET /healthz` — service and D1 health.

Friends filtering is deliberately not an authority-cutover requirement. The endpoint reports it as unavailable until a client-Steam-friends filtering path is enabled.

Global reads use deterministic cursor pagination. One immutable board revision admits only one gameplay digest for each official vehicle content ID, so a vehicle rebalance must ship with a new Time Attack ruleset/board revision rather than silently mixing physics revisions.

## Operations

Create a complete D1 and replay-object backup with:

```powershell
./scripts/backup.ps1 -Environment production -OutputRoot D:\mxt-leaderboard-backups
```

The backup is marked complete only after every replay is downloaded and verified against its D1 SHA-256 and byte length. Restore targets must already have current migrations and contain zero leaderboard records:

```powershell
./scripts/restore.ps1 -Environment staging -BackupRoot D:\mxt-leaderboard-backups\production-YYYYMMDD-HHMMSS -ConfirmEmptyTargetRestore
```

Restores checksum all local files, upload replay objects first, and commit the D1 export last. The script refuses to merge into or overwrite a populated authority.

Moderation never destroys replay evidence. Use `scripts/moderate.py` with the environment's untracked admin-secret file to set a run, historical score, or player to `visible`, `quarantined`, or `hidden`. Every action records its operator, reason, target, state, and timestamp in `moderation_actions`. Moderating a run also atomically repairs its per-vehicle best pointer.
