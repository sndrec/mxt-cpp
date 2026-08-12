param(
    [string]$HostName = '127.0.0.1',
    [int]$Port = 8787
)

$ErrorActionPreference = 'Stop'
$response = Invoke-RestMethod -Method Get -Uri "http://${HostName}:${Port}/healthz" -TimeoutSec 5
if ($response.ok -ne $true) {
    throw 'Leaderboard verifier health check returned an unhealthy response.'
}
Write-Host 'MaxX Throttle leaderboard verifier is healthy.'
