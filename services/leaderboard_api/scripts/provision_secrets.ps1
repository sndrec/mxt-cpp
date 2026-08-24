param(
    [ValidateSet('staging', 'production')]
    [string]$Environment
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

$ingestPath = Join-Path $secretRoot "$Environment-ingest-secret.txt"
$replayUrlPath = Join-Path $secretRoot "$Environment-replay-url-secret.txt"
$ingestSecret = New-RandomHex 48
$replayUrlSecret = New-RandomHex 48

Set-Content -LiteralPath $ingestPath -Value $ingestSecret -NoNewline -Encoding ascii
Set-Content -LiteralPath $replayUrlPath -Value $replayUrlSecret -NoNewline -Encoding ascii
Set-WorkerSecret 'INGEST_SECRET' $ingestSecret
Set-WorkerSecret 'REPLAY_URL_SECRET' $replayUrlSecret

Write-Host "Provisioned $Environment Worker secrets."
Write-Host "Verifier ingest secret: $ingestPath"
Write-Host "Replay URL secret: $replayUrlPath"
