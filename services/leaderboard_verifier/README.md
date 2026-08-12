# MaxX Throttle trusted leaderboard verifier

The full Partner configuration, credential-handling, provisioning, deployment, and live acceptance procedure is in `docs/steamworks_launch_checklist.md`.

This service is the only score-writing component. The game client can read Steam leaderboards and obtain a Web API authentication ticket, but it has no score-upload binding. A submission is accepted only after the service:

1. authenticates the ticket against the configured identity;
2. confirms that the returned Steam account owns the game;
3. rejects reuse of that authentication ticket;
4. runs the shipped game headlessly with `--leaderboard-replay-verify`;
5. checks the re-simulated board tuple and millisecond score against the request and checked-in manifest;
6. calls Steam's publisher-only `SetLeaderboardScore` endpoint with `KeepBest`.

The publisher key is read only by this process and is removed from the child verifier process environment.

## Build a pinned Windows deployment bundle

Build the release GDExtension first, then assemble one verifier directory. The
bundle contains a release-exported verifier executable and PCK, its raw
authoritative track/vehicle data, the Python service, operational scripts, and a
SHA-256 identity record. It intentionally does not contain the publisher key,
numeric leaderboard IDs, curated-package
archive, or `steam_appid.txt`.

```powershell
scons target=template_release -j4
.\services\leaderboard_verifier\build_windows_bundle.ps1 `
  -Destination C:\mxt-verifier\bundle
```

By default, the builder takes the authoritative base track and vehicle files
from the sibling Steam ContentBuilder tree. Pass `-BaseContentDirectory` when
building from a different depot staging directory.

Create the bundle from a clean committed checkout for production. Start it with
external deployment files:

```powershell
C:\mxt-verifier\bundle\start_windows_bundle.ps1 `
  -PublisherKeyFile C:\secure\steam_publisher_key.txt `
  -LeaderboardIdsFile C:\secure\mxt-leaderboard-ids-5001310.json `
  -CuratedWorkshopPackagesFile C:\secure\curated-workshop-packages.json
```

Run `health_check.ps1` locally after startup. The launcher validates required
files and key shape, supplies only absolute deployment paths, and clears its
publisher-key environment variable when the service exits.

For the public edge, create a named Cloudflare Tunnel and adapt
`cloudflared.example.yml`. Its ingress rules send only the exact submission path
to the loopback service and return 404 for every other public request. Keep
`/healthz` loopback-only. A Quick Tunnel is suitable for a disposable acceptance
run but has no stable hostname or uptime commitment and is not the production
deployment.

## Provision the Steam boards

Set credentials in the process environment. Never put the publisher key in a file inside the game repository or a client depot. Prefer reading it from a restricted temporary file outside the repository and remove the environment variable after the process exits; do not paste it into a shell history or chat.

```powershell
$env:MXT_STEAM_APP_ID = '<main game app id>'
$env:MXT_STEAM_AUTH_APP_IDS = '<main game app id>,<optional playtest app id>'
$env:MXT_STEAM_PUBLISHER_KEY = '<publisher web api key>'
python services/leaderboard_verifier/provision_leaderboards.py `
  --output C:\secure\mxt-leaderboard-ids.json
```

The command creates every board in `mxto/steam/leaderboards.json` as ascending, millisecond-display, public-read, trusted-write-only and then records Steam's numeric IDs. Keep that generated file with the deployed service, not in a client build.

## Run the service

Use either an exported dedicated verifier build:

```powershell
$env:MXT_GAME_EXECUTABLE = 'C:\mxt-verifier\MaxXThrottle.exe'
```

or a pinned Godot executable and project tree:

```powershell
$env:MXT_GODOT_EXE = 'C:\Godot\Godot_console.exe'
$env:MXT_GODOT_PROJECT = 'C:\mxt-verifier\mxto'
```

Then configure and start the process:

```powershell
$env:MXT_STEAM_APP_ID = '<main game app id>'
$env:MXT_STEAM_PUBLISHER_KEY = '<publisher web api key>'
$env:MXT_STEAM_LEADERBOARD_IDS = 'C:\secure\mxt-leaderboard-ids.json'
$env:MXT_STEAM_TICKET_IDENTITY = 'mxt-leaderboard-v1'
$env:MXT_LEADERBOARD_LISTEN_HOST = '127.0.0.1'
$env:MXT_LEADERBOARD_LISTEN_PORT = '8787'
python services/leaderboard_verifier/server.py
```

Terminate TLS at a reverse proxy and expose only `POST /v1/time-attack/submit`. Keep `/healthz` available to the local health checker. The service intentionally listens on loopback by default. Set reverse-proxy request limits at or below `MXT_MAX_REPLAY_BYTES` (default 64 MiB), do not log `Authorization` headers, and run only the exact game build associated with the checked-in manifest.

The submission request is:

```text
POST /v1/time-attack/submit
Content-Type: application/vnd.mxt.replay+json
Authorization: SteamTicket <hex ticket>
X-MXT-Ticket-Identity: mxt-leaderboard-v1
X-MXT-Steam-App-ID: <the client Steam App ID>
X-MXT-Board: <manifest steam_name>
X-MXT-Claimed-Score-Milliseconds: <integer>

<raw .replay.json bytes>
```

Successful responses contain the authenticated Steam ID, verified board name, and verified score. Tickets are single-use within one service process. For horizontal deployment, replace the in-process replay guard with a shared atomic TTL store before adding a second instance.

## Curate a community track revision

Run the curation script against the exact validated Workshop package directory. It emits a reviewed copy of the leaderboard manifest; it never edits the checked-in manifest implicitly.

```powershell
Godot_console.exe --headless --path mxto --script res://steam/curate_workshop_track.gd -- `
  --package 'C:\Steam\workshop\content\<app id>\<item id>' `
  --workshop-id '<item id>' `
  --slug 'community-track-slug' `
  --output 'C:\review\leaderboards.json'
```

Review the new `curated_workshop` record and replace `mxto/steam/leaderboards.json` with the reviewed output. Provision again to create its trusted board. The record binds the Workshop ID to the exact validated gameplay digest; updating the Workshop item does not update or qualify for the board.

The verifier deployment must retain every curated package revision. Set `MXT_CURATED_WORKSHOP_PACKAGES` to a JSON file mapping each curated gameplay digest to its archived canonical package directory:

```json
{
  "sha256:0123456789abcdef...": "C:\\mxt-verifier\\curated\\1234567890\\sha256-package-directory"
}
```

The service refuses to start unless this map exactly covers the curated gameplay digests in the manifest. It passes only the requested board's archived revision into the isolated verifier, so multiple historical digests from one mutable Workshop listing remain independently verifiable.
