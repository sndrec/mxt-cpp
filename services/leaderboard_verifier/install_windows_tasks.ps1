param(
    [string]$ServerRoot = (Join-Path $env:LOCALAPPDATA 'MaxXThrottle\server'),
    [switch]$StartTasks,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$serverRootPath = [IO.Path]::GetFullPath($ServerRoot)
$bundleRoot = Join-Path $serverRootPath 'verifier-bundle'
$bundleLauncher = Join-Path $bundleRoot 'start_windows_bundle.ps1'
$publisherKey = Join-Path $serverRootPath 'steam_publisher_key.txt'
$leaderboardIds = Join-Path $serverRootPath 'mxt-leaderboard-ids-5001310.json'
$curatedPackages = Join-Path $serverRootPath 'curated-workshop-packages.json'
$tunnelToken = Join-Path $serverRootPath 'cloudflare-tunnel-token.txt'
$cloudflared = (Get-Command cloudflared.exe -ErrorAction Stop).Source
$powershell = (Get-Command powershell.exe -ErrorAction Stop).Source

foreach ($required in @($bundleLauncher, $publisherKey, $leaderboardIds, $tunnelToken)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required deployment file is missing: $required"
    }
}
$settings = New-ScheduledTaskSettingsSet `
    -ExecutionTimeLimit (New-TimeSpan -Days 3650) `
    -RestartCount 999 `
    -RestartInterval (New-TimeSpan -Minutes 1) `
    -MultipleInstances IgnoreNew `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$verifierArguments = @(
    '-NoProfile',
    '-ExecutionPolicy Bypass',
    "-File `"$bundleLauncher`"",
    "-PublisherKeyFile `"$publisherKey`"",
    "-LeaderboardIdsFile `"$leaderboardIds`""
)
if (Test-Path -LiteralPath $curatedPackages -PathType Leaf) {
    $verifierArguments += "-CuratedWorkshopPackagesFile `"$curatedPackages`""
}
$verifierAction = New-ScheduledTaskAction `
    -Execute $powershell `
    -Argument ($verifierArguments -join ' ') `
    -WorkingDirectory $bundleRoot
$tunnelAction = New-ScheduledTaskAction `
    -Execute $cloudflared `
    -Argument "tunnel --no-autoupdate --loglevel info run --token-file `"$tunnelToken`"" `
    -WorkingDirectory $serverRootPath

$tasks = @(
    @{
        Name = 'MaxXThrottleLeaderboardVerifier'
        Action = $verifierAction
        Description = 'MaxX Throttle trusted Steam leaderboard verifier'
    },
    @{
        Name = 'MaxXThrottleLeaderboardTunnel'
        Action = $tunnelAction
        Description = 'Cloudflare Tunnel for the MaxX Throttle trusted leaderboard verifier'
    }
)

foreach ($task in $tasks) {
    $existing = Get-ScheduledTask -TaskName $task.Name -ErrorAction SilentlyContinue
    if ($null -eq $existing -or $Force) {
        if ($null -ne $existing -and $existing.State -eq 'Running') {
            Stop-ScheduledTask -TaskName $task.Name
        }
        Register-ScheduledTask `
            -TaskName $task.Name `
            -Action $task.Action `
            -Trigger $trigger `
            -Settings $settings `
            -Description $task.Description `
            -Force | Out-Null
        Write-Host "Registered $($task.Name)."
    } else {
        Write-Host "$($task.Name) already exists."
    }
    if ($StartTasks) {
        Start-ScheduledTask -TaskName $task.Name
        Write-Host "Started $($task.Name)."
    }
}
