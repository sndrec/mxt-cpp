param(
    [ValidateSet('staging', 'production')]
    [string]$Environment,
    [switch]$Rotate
)

$ErrorActionPreference = 'Stop'
$serviceRoot = Split-Path -Parent $PSScriptRoot
$secretRoot = Join-Path $serviceRoot 'local-secrets'
New-Item -ItemType Directory -Path $secretRoot -Force | Out-Null

function New-RandomHex([int]$ByteCount) {
    $bytes = New-Object byte[] $ByteCount
    $generator = [Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $generator.GetBytes($bytes)
    } finally {
        $generator.Dispose()
    }
    return ([BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
}

function Set-WorkerSecret([string]$Name, [string]$Value) {
    $Value | & npx.cmd wrangler secret put $Name --env $Environment
    if ($LASTEXITCODE -ne 0) {
        throw "Wrangler failed to set $Name for $Environment."
    }
}

function Get-OrCreateSecret([string]$Path) {
    if (-not $Rotate -and (Test-Path -LiteralPath $Path -PathType Leaf)) {
        $existing = (Get-Content -LiteralPath $Path -Raw).Trim()
        if ($existing.Length -ge 64) {
            return $existing
        }
        throw "Existing secret file is invalid: $Path"
    }
    $value = New-RandomHex 48
    Set-Content -LiteralPath $Path -Value $value -NoNewline -Encoding ascii
    return $value
}

$ingestPath = Join-Path $secretRoot "$Environment-ingest-secret.txt"
$replayUrlPath = Join-Path $secretRoot "$Environment-replay-url-secret.txt"
$migrationPath = Join-Path $secretRoot "$Environment-migration-secret.txt"
$ingestSecret = Get-OrCreateSecret $ingestPath
$replayUrlSecret = Get-OrCreateSecret $replayUrlPath
$migrationSecret = Get-OrCreateSecret $migrationPath
Set-WorkerSecret 'INGEST_SECRET' $ingestSecret
Set-WorkerSecret 'REPLAY_URL_SECRET' $replayUrlSecret
Set-WorkerSecret 'MIGRATION_SECRET' $migrationSecret

Write-Host "Provisioned $Environment Worker secrets."
Write-Host "Verifier ingest secret: $ingestPath"
Write-Host "Replay URL secret: $replayUrlPath"
Write-Host "Migration secret: $migrationPath"
