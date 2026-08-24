# MaxX Throttle Leaderboard API

This Cloudflare Worker is the authoritative metadata and replay store for ranked Time Attack. The native verifier remains responsible for Steam authentication, app ownership, official-content validation, and deterministic replay simulation. Only its signed output can enter this service.

## Storage

- D1 stores Steam identities, board identities, immutable verified runs, and conditional per-vehicle best references.
- R2 stores every successfully verified completed ranked replay by SHA-256.
- Slower runs remain in `verified_runs` and R2 even when `player_vehicle_bests` does not change.
- Practice, unranked, custom-track, and non-official-vehicle runs never reach the ingest endpoint.

The ingest operation stores and checksum-validates the replay object before committing its D1 metadata. Retrying the same authenticated Steam ID and replay digest is idempotent.

## Local development

1. Copy `.dev.vars.example` to `.dev.vars` and replace both development secrets.
2. Install dependencies with `npm install`.
3. Generate bindings with `npm run types`.
4. Run `npm run check` and `npm test`.
5. Run `npx wrangler d1 migrations apply mxt-leaderboards-development --local` before interactive `npx wrangler dev` use.

The integration tests use the current Cloudflare Vitest plugin and real local D1/R2 bindings.

## Cloud resources

`wrangler.jsonc` initially contains a zero D1 identifier so local type generation and tests have a complete binding contract. Before deploying a real environment:

1. Create separate development and production D1 databases and R2 buckets.
2. Replace the placeholder database ID and bind the intended bucket.
3. Set `INGEST_SECRET` and `REPLAY_URL_SECRET` with `wrangler secret put`; never add their values to the config.
4. Apply D1 migrations remotely.
5. Run `npm run check`, `npm test`, `npm run deploy:dry`, and `npx wrangler check startup`.
6. Deploy and verify `/healthz` before configuring the native verifier or game client.

The verifier and Worker must share `INGEST_SECRET`. `REPLAY_URL_SECRET` belongs only to the Worker.

## API

- `POST /v1/ingest/verified-run` — private HMAC-authenticated verifier ingest with the replay as its body.
- `GET /v1/leaderboards/{board_id}` — Global or Around Me board, optionally filtered by vehicle gameplay digest.
- `GET /v1/boards/{board_id}/categories` — available vehicle categories.
- `GET /v1/boards/{board_id}/players/{steam_id}/bests` — one player's per-vehicle bests.
- `GET /v1/runs/{run_id}/replay-url` — short-lived replay URL authorization.
- `GET /v1/replays/{run_id}` — signed replay download.
- `GET /healthz` — service and D1 health.

Friends filtering is deliberately not an authority-cutover requirement. The endpoint reports it as unavailable until a client-Steam-friends filtering path is enabled.
