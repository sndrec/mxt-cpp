# MaxX Throttle Steamworks launch checklist

## Application identities

- Main game and production leaderboard App ID: `5001310`.
- Steam Playtest App ID: `5001340`.
- Production Workshop consumer: main game `5001310`.

Keep `steam_appid.txt` out of depots. The runtime App ID is reported by `MxtSteamService` and sent with leaderboard authentication tickets. The trusted service may authenticate both App IDs, but Steam leaderboard IDs and entries remain scoped to the App ID under which each board was provisioned.

## Partner-site inspection and configuration

Complete this section while signed into the Steamworks Partner Site. Configuration changes are not live until they are prepared and published to Steam.

### Main game `5001310`

1. Under Steam Cloud, set a nonzero per-user byte quota and file-count quota sufficient for Workshop preview images.
2. Under Workshop configuration, enable **ISteamUGC for file transfer**.
3. Configure the Workshop tags used by the game exactly as:
   - `Vehicle`
   - `Track`
   - `Format Revision 1`
4. Review Workshop legal text, moderation permissions, banned words, visibility options, and daily/item quotas.
5. Publish the Steam Cloud and Workshop configuration changes.
6. Create a private or Friends Only item from a Steam account that owns the main app, accept the Workshop legal agreement when prompted, and verify update/download callbacks.
7. Provision the production trusted leaderboards from `mxto/steam/leaderboards.json` using App ID `5001310`.

### Playtest `5001340`

The Playtest is a distinct application. Confirm in the Partner Site which Workshop configuration Valve exposes for the Playtest and run a private item experiment before assuming cross-app subscription behavior. The production design remains one main-game Workshop; do not deliberately create a second permanent Playtest Workshop.

If Steam will not let a process running as `5001340` create, subscribe to, and receive installs for `5001310` Workshop items, use main-app release-override access for the two-account Workshop acceptance test. Do not work around that platform boundary by shipping a second content ecosystem.

For pre-release leaderboard testing, provision a temporary mirror of the checked-in boards under `5001340`. Run a verifier configured with `MXT_STEAM_APP_ID=5001340` and remove or abandon those Playtest-only scores after testing. Production boards are provisioned separately under `5001310`; numeric leaderboard IDs are never shared between the apps.

## Credential handling

Never provide a Steam password, Steam Guard code, or publisher key in chat. Sign into the Partner Site interactively. Store the publisher Web API key in a temporary text file outside the repository and client depot, restricted to the deployment account.

This PowerShell pattern loads the key without printing it:

```powershell
$keyPath = 'C:\secure\mxt-publisher-key.txt'
$env:MXT_STEAM_PUBLISHER_KEY = (Get-Content -Raw -LiteralPath $keyPath).Trim()
try {
    # Run provisioning or the verifier here.
} finally {
    Remove-Item Env:MXT_STEAM_PUBLISHER_KEY -ErrorAction SilentlyContinue
}
```

The publisher key needs access to the target application and these publisher-only Web API operations:

- `ISteamLeaderboards/FindOrCreateLeaderboard`
- `ISteamLeaderboards/GetLeaderboardsForGame`
- `ISteamLeaderboards/SetLeaderboardScore`
- `ISteamUserAuth/AuthenticateUserTicket`
- `ISteamUser/CheckAppOwnership`

## Provision production boards

Choose a service-private output directory. The generated numeric IDs are deployment configuration, not client content.

```powershell
$keyPath = 'C:\secure\mxt-publisher-key.txt'
$env:MXT_STEAM_APP_ID = '5001310'
$env:MXT_STEAM_PUBLISHER_KEY = (Get-Content -Raw -LiteralPath $keyPath).Trim()
try {
    python services/leaderboard_verifier/provision_leaderboards.py `
        --output C:\secure\mxt-leaderboard-ids-5001310.json
} finally {
    Remove-Item Env:MXT_STEAM_PUBLISHER_KEY -ErrorAction SilentlyContinue
}
```

Inspect the generated file and confirm it contains exactly the 31 checked-in board names and positive numeric IDs. Re-running provisioning is safe: it finds existing names before creating missing ones.

## Start a local trusted verifier

Use the exact game build whose content digests are in the leaderboard manifest. The local service can authenticate Playtest and main-game tickets while writing only to the target App ID's boards.

```powershell
$keyPath = 'C:\secure\mxt-publisher-key.txt'
$env:MXT_STEAM_APP_ID = '5001310'
$env:MXT_STEAM_AUTH_APP_IDS = '5001310,5001340'
$env:MXT_STEAM_PUBLISHER_KEY = (Get-Content -Raw -LiteralPath $keyPath).Trim()
$env:MXT_STEAM_LEADERBOARD_IDS = 'C:\secure\mxt-leaderboard-ids-5001310.json'
$env:MXT_STEAM_TICKET_IDENTITY = 'mxt-leaderboard-v1'
$env:MXT_GODOT_EXE = 'A:\programs\Godot_v4.7.1-stable_win64\Godot_v4.7.1-stable_win64_console.exe'
$env:MXT_GODOT_PROJECT = 'B:\programming\mxt-cpp\mxto'
$env:MXT_LEADERBOARD_LISTEN_HOST = '127.0.0.1'
$env:MXT_LEADERBOARD_LISTEN_PORT = '8787'
python services/leaderboard_verifier/server.py
```

For public deployment, pin the verifier executable/project and manifest together, terminate TLS at a reverse proxy, expose only `POST /v1/time-attack/submit`, keep `/healthz` private, impose a 64 MiB request limit, and never log `Authorization`. Do not put the publisher key, numeric board-ID file, or curated package archive in the client depot.

Set `mxto/steam/leaderboard_service.json` `base_url` only to the final public HTTPS origin. Localhost HTTP remains accepted for local integration testing; non-local cleartext HTTP is rejected by the client.

## Live Workshop acceptance matrix

Use two ordinary Steam accounts. Do not use local file copying as a substitute for any step.

1. Account A authors and validates one vehicle, publishes it Friends Only, accepts the agreement, and records its Published File ID and package/gameplay digests.
2. Account B subscribes, observes download/install progress, selects the vehicle, and launches an unranked race.
3. Account A updates the same item. Account B receives the update; the package digest changes and stale local bytes are not admitted.
4. Both accounts join one multiplayer race using the exact vehicle. The peer missing it must download and validate before becoming ready.
5. Repeat the publish, subscribe, update, install, select, and multiplayer-admission flow for a packaged track.
6. Attempt malformed, oversized, wrong-revision, digest-mismatched, and missing packages. Each must remain unavailable with an explicit status.

Record account IDs, App ID used, Published File IDs, package/gameplay digests, visibility, agreement state, and the relevant client logs in a private acceptance record.

## Live trusted-leaderboard acceptance matrix

1. Complete a legitimate official Time Attack under the forced rules: bumpers off, S-BOOST off, vehicle restore on.
2. Confirm the client queues the replay, obtains a fresh Web API ticket, and submits to the verifier.
3. Confirm the verifier authenticates the ticket, checks ownership, re-simulates the replay, and writes KeepBest to the expected track board.
4. Confirm global, around-user, and friends reads show the same score.
5. Submit slower and faster legitimate runs and verify KeepBest behavior.
6. Confirm explicit rejection of custom content, debug/automation sessions, changed rules, wrong track/board tuples, altered replay bytes, mismatched content digests, reused tickets, and scores that disagree with re-simulation.
7. Stop the verifier or network, complete a run, and confirm the pending queue survives restart and later submits with a new ticket.
8. Confirm there is no client-callable score-write API or direct upload path.

Phase 7 is not complete until the production-equivalent HTTPS deployment passes this matrix. Local replay verification alone is not a substitute for Steam authentication and a real trusted write.

## Current content-budget profile

The initial revision-1 limits remain intentionally above the current built-in payloads while bounding hostile content:

- 31 official tracks: 0.36-7.32 MiB for authoritative track, GLB visual, and metadata combined; mean 2.78 MiB.
- Four built-in vehicle source payloads: approximately 1.36-3.30 MiB before canonical GLB packaging.
- Representative validated GLB smoke asset: 0.57 MiB, 13,931 vertices, 23,707 triangles, one 512 by 512 texture.

The public ceilings remain 64 MiB per vehicle package, 512 MiB per track package, 250,000 vehicle triangles, 2,000,000 track triangles, 4096 pixels per texture dimension, 64 MiPixels aggregate for vehicle textures, and 256 MiPixels aggregate for track textures. Re-profile loading time and peak VRAM on low-spec hardware before public Workshop launch; tightening limits within revision 1 is allowed before release.
