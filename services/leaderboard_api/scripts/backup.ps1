param(
    [ValidateSet('staging', 'production')]
    [Parameter(Mandatory = $true)]
    [string]$Environment,
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
$serviceRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $serviceRoot 'backups'
}
$timestamp = [DateTimeOffset]::UtcNow.ToString('yyyyMMdd-HHmmss')
$backupRoot = Join-Path $OutputRoot "$Environment-$timestamp"
$objectsRoot = Join-Path $backupRoot 'objects'
New-Item -ItemType Directory -Path $objectsRoot -Force | Out-Null
$databasePath = Join-Path $backupRoot 'database.sql'
$bucket = if ($Environment -eq 'production') {
    'mxt-leaderboard-replays-production'
} else {
    'mxt-leaderboard-replays-staging'
}

Push-Location $serviceRoot
try {
    & npx.cmd wrangler d1 export DB --env $Environment --remote --output $databasePath --skip-confirmation
    if ($LASTEXITCODE -ne 0) { throw 'D1 export failed.' }
    $inventoryJson = (& npx.cmd wrangler d1 execute DB --env $Environment --remote --json --command `
        'SELECT replay_sha256, object_key, byte_length FROM replay_objects ORDER BY replay_sha256' | Out-String)
    if ($LASTEXITCODE -ne 0) { throw 'Replay inventory query failed.' }
    $inventoryResult = $inventoryJson | ConvertFrom-Json
    $objects = @($inventoryResult[0].results)
    $manifestObjects = @()
    foreach ($record in $objects) {
        $hex = [string]$record.replay_sha256
        if (-not $hex.StartsWith('sha256:')) { throw "Invalid replay digest in D1: $hex" }
        $hex = $hex.Substring(7)
        $localName = "$hex.mxt_replay"
        $localPath = Join-Path $objectsRoot $localName
        & npx.cmd wrangler r2 object get "$bucket/$($record.object_key)" --remote --file $localPath
        if ($LASTEXITCODE -ne 0) { throw "R2 download failed for $($record.object_key)." }
        $actualHash = (Get-FileHash -LiteralPath $localPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $actualLength = (Get-Item -LiteralPath $localPath).Length
        if ($actualHash -ne $hex -or $actualLength -ne [long]$record.byte_length) {
            throw "Replay backup integrity check failed for $($record.object_key)."
        }
        $manifestObjects += [ordered]@{
            replay_sha256 = [string]$record.replay_sha256
            object_key = [string]$record.object_key
            byte_length = [long]$record.byte_length
            file = "objects/$localName"
        }
    }
    $manifest = [ordered]@{
        format_revision = 1
        complete = $true
        environment = $Environment
        created_utc = [DateTimeOffset]::UtcNow.ToString('o')
        d1_file = 'database.sql'
        d1_sha256 = 'sha256:' + (Get-FileHash -LiteralPath $databasePath -Algorithm SHA256).Hash.ToLowerInvariant()
        r2_bucket = $bucket
        replay_objects = $manifestObjects
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backupRoot 'manifest.json') -Encoding utf8
} finally {
    Pop-Location
}
Write-Host "Complete leaderboard backup: $backupRoot"
