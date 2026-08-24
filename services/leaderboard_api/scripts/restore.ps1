param(
    [ValidateSet('staging', 'production')]
    [Parameter(Mandatory = $true)]
    [string]$Environment,
    [Parameter(Mandatory = $true)]
    [string]$BackupRoot,
    [switch]$ConfirmEmptyTargetRestore
)

$ErrorActionPreference = 'Stop'
if (-not $ConfirmEmptyTargetRestore) {
    throw 'Restore requires -ConfirmEmptyTargetRestore and refuses to overwrite a populated authority.'
}
$serviceRoot = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $BackupRoot 'manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.format_revision -ne 1 -or $manifest.complete -ne $true) {
    throw 'Backup manifest is incomplete or unsupported.'
}
$databasePath = Join-Path $BackupRoot ([string]$manifest.d1_file)
$targetBucket = if ($Environment -eq 'production') {
    'mxt-leaderboard-replays-production'
} else {
    'mxt-leaderboard-replays-staging'
}
$databaseHash = 'sha256:' + (Get-FileHash -LiteralPath $databasePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($databaseHash -ne [string]$manifest.d1_sha256) { throw 'D1 backup digest mismatch.' }

foreach ($record in @($manifest.replay_objects)) {
    $localPath = Join-Path $BackupRoot ([string]$record.file)
    $actualHash = 'sha256:' + (Get-FileHash -LiteralPath $localPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne [string]$record.replay_sha256 -or
            (Get-Item -LiteralPath $localPath).Length -ne [long]$record.byte_length) {
        throw "Replay backup integrity check failed for $($record.object_key)."
    }
}

Push-Location $serviceRoot
try {
    $countJson = (& npx.cmd wrangler d1 execute DB --env $Environment --remote --json --command `
        'SELECT (SELECT COUNT(*) FROM verified_runs) + (SELECT COUNT(*) FROM historical_scores) AS count' | Out-String)
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect restore target.' }
    $countResult = $countJson | ConvertFrom-Json
    if ([long]$countResult[0].results[0].count -ne 0) {
        throw 'Restore target contains leaderboard records; refusing to merge or overwrite them.'
    }
    foreach ($record in @($manifest.replay_objects)) {
        $localPath = Join-Path $BackupRoot ([string]$record.file)
        & npx.cmd wrangler r2 object put "$targetBucket/$($record.object_key)" `
            --remote --file $localPath --content-type 'application/vnd.mxt.replay' --force
        if ($LASTEXITCODE -ne 0) { throw "R2 restore failed for $($record.object_key)." }
    }
    & npx.cmd wrangler d1 execute DB --env $Environment --remote --file $databasePath --yes
    if ($LASTEXITCODE -ne 0) { throw 'D1 restore failed.' }
} finally {
    Pop-Location
}
Write-Host "Leaderboard restore completed from $BackupRoot"
