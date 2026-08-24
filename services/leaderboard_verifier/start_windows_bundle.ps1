param(
    [Parameter(Mandatory = $true)]
    [string]$PublisherKeyFile,
    [Parameter(Mandatory = $true)]
    [string]$LeaderboardIngestSecretFile,
    [string]$LeaderboardApiUrl = 'https://mxt-leaderboard-api-production.sndrec32exe.workers.dev',
    [int]$AppId = 5001340,
    [string]$AuthenticatedAppIds = '5001340',
    [string]$ListenHost = '127.0.0.1',
    [int]$ListenPort = 8787,
    [int]$VerifierConcurrency = 2,
    [string]$PythonExe = 'python'
)

$ErrorActionPreference = 'Stop'
$bundleRoot = $PSScriptRoot
$publisherKeyPath = (Resolve-Path -LiteralPath $PublisherKeyFile).Path
$leaderboardIngestSecretPath = (Resolve-Path -LiteralPath $LeaderboardIngestSecretFile).Path
$verifierExecutable = Join-Path $bundleRoot 'runtime\MaxXThrottleVerifier.exe'
$manifest = Join-Path $bundleRoot 'config\leaderboards.json'

foreach ($required in @($verifierExecutable, $manifest, (Join-Path $bundleRoot 'service\server.py'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Verifier bundle is incomplete: $required"
    }
}
$publisherKey = (Get-Content -LiteralPath $publisherKeyPath -Raw).Trim()
if ($publisherKey -notmatch '^[0-9A-Fa-f]{32}$') {
    throw 'The publisher key file must contain exactly one 32-character hexadecimal key.'
}
$leaderboardIngestSecret = (Get-Content -LiteralPath $leaderboardIngestSecretPath -Raw).Trim()
if ($leaderboardIngestSecret.Length -lt 64) {
    throw 'The leaderboard ingest secret must contain at least 64 characters.'
}
if ($ListenPort -lt 1 -or $ListenPort -gt 65535) {
    throw 'ListenPort must be between 1 and 65535.'
}

$env:MXT_STEAM_APP_ID = [string]$AppId
$env:MXT_STEAM_AUTH_APP_IDS = $AuthenticatedAppIds
$env:MXT_STEAM_PUBLISHER_KEY = $publisherKey
$env:MXT_STEAM_TICKET_IDENTITY = 'mxt-leaderboard-v1'
$env:MXT_LEADERBOARD_API_URL = $LeaderboardApiUrl.TrimEnd('/')
$env:MXT_LEADERBOARD_INGEST_SECRET = $leaderboardIngestSecret
$env:MXT_LEADERBOARD_LISTEN_HOST = $ListenHost
$env:MXT_LEADERBOARD_LISTEN_PORT = [string]$ListenPort
$env:MXT_VERIFIER_CONCURRENCY = [string]$VerifierConcurrency
$env:MXT_GAME_EXECUTABLE = $verifierExecutable
Remove-Item Env:MXT_GODOT_EXE -ErrorAction SilentlyContinue
Remove-Item Env:MXT_GODOT_PROJECT -ErrorAction SilentlyContinue
$env:MXT_LEADERBOARD_MANIFEST = $manifest
$certifiPath = (& $PythonExe -c 'import certifi; print(certifi.where())' 2>$null).Trim()
if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $certifiPath -PathType Leaf)) {
    $env:SSL_CERT_FILE = $certifiPath
}
try {
    & $PythonExe (Join-Path $bundleRoot 'service\server.py')
    exit $LASTEXITCODE
} finally {
    Remove-Item Env:MXT_STEAM_PUBLISHER_KEY -ErrorAction SilentlyContinue
    Remove-Item Env:MXT_LEADERBOARD_INGEST_SECRET -ErrorAction SilentlyContinue
}
