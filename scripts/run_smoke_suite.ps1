[CmdletBinding()]
param(
    [ValidateSet('stable', 'simulation', 'replay', 'lobby-load', 'all-applicable', 'self-check', 'list')]
    [string[]]$Group = @('stable'),
    [string]$GodotPath = 'A:\programs\Godot_v4.7.1-stable_win64\Godot_v4.7.1-stable_win64.exe',
    [string]$ReplayPath = '',
    [string]$TracksDirectory = '',
    [ValidateRange(1, 63)]
    [int]$LobbyClients = 40,
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath 'variable:PSNativeCommandUseErrorActionPreference') {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repoRoot 'mxto'
$lobbyLoadRunner = Join-Path $PSScriptRoot 'run_lobby_load_test.ps1'
$godotCommandPath = $GodotPath
if ($GodotPath.EndsWith('.exe', [System.StringComparison]::OrdinalIgnoreCase) -and
    -not $GodotPath.EndsWith('_console.exe', [System.StringComparison]::OrdinalIgnoreCase)) {
    $consoleName = [System.IO.Path]::GetFileNameWithoutExtension($GodotPath) + '_console.exe'
    $consolePath = Join-Path ([System.IO.Path]::GetDirectoryName($GodotPath)) $consoleName
    if (Test-Path -LiteralPath $consolePath -PathType Leaf) {
        $godotCommandPath = $consolePath
    }
}

$stableScripts = @(
    'car_livery_render_manager_smoke.gd'
    'car_livery_serialization_smoke.gd'
    'car_livery_stamp_mesh_smoke.gd'
    'car_properties_sampler_smoke.gd'
    'car_settings_preview_input_smoke.gd'
    'car_stat_state_matrix_smoke.gd'
    'car_creator_completion_smoke.gd'
    'content_package_validation_smoke.gd'
    'custom_stamp_network_controller_smoke.gd'
    'grand_prix_grid_results_smoke.gd'
    'lobby_chibi_render_smoke.gd'
    'lobby_empty_track_sequence_smoke.gd'
    'lobby_scale_smoke.gd'
    'netplay_admission_state_smoke.gd'
    'netplay_input_resilience_smoke.gd'
    'netplay_state_transfer_smoke.gd'
    'race_audio_controller_smoke.gd'
    'race_hud_sticker_pool_smoke.gd'
    'text_chat_history_smoke.gd'
    'track_content_controller_smoke.gd'
    'track_scene_lobby_hide_smoke.gd'
    'vehicle_content_controller_smoke.gd'
    'vehicle_restore_elimination_smoke.gd'
    'voice_capture_dynamics_smoke.gd'
)

$simulationScripts = @(
    'auth_input_packet_roundtrip.gd'
    'bumper_netstate_smoke.gd'
    'lobby_bumper_smoke.gd'
    'netstate_native_range_equivalence.gd'
    'netstate_restore_equivalence.gd'
)

function Write-GroupList {
    Write-Output 'stable         Single-process scene, UI, content, and controller smoke tests.'
    Write-Output 'simulation     Deterministic input, bumper, and netstate checks.'
    Write-Output 'replay         Replay controller smoke; requires -ReplayPath.'
    Write-Output 'lobby-load     Multi-process lobby load; requires run_lobby_load_test.ps1.'
    Write-Output 'all-applicable Stable and simulation groups plus optional groups whose prerequisites were supplied.'
    Write-Output 'self-check     Verify that a failing child process is detected.'
    Write-Output 'list           Print this list without running tests.'
}

function Add-Failure {
    param(
        [System.Collections.Generic.List[string]]$Failures,
        [string]$Name,
        [int]$ExitCode
    )

    $Failures.Add("$Name (exit $ExitCode)")
}

function Test-LogForEngineFailure {
    param([string[]]$Lines)

    foreach ($line in $Lines) {
        if ($line -match '^(SCRIPT ERROR|ERROR: Failed to load script|ERROR: Parse Error|ERROR: Cannot open file)') {
            return $true
        }
    }
    return $false
}

function Invoke-GodotScript {
    param(
        [string]$ScriptName,
        [string[]]$UserArguments,
        [System.Collections.Generic.List[string]]$Failures
    )

    $scriptPath = Join-Path $projectPath (Join-Path 'test' $ScriptName)
    if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
        Add-Failure -Failures $Failures -Name $ScriptName -ExitCode 2
        Write-Error "Missing Godot test script: $scriptPath"
        return
    }

    $arguments = @(
        '--headless'
        '--quit-after'
        '600'
        '--path'
        $projectPath
        '--script'
        "res://test/$ScriptName"
    )
    if ($UserArguments.Count -gt 0) {
        $arguments += '--'
        $arguments += $UserArguments
    }

    $logPath = Join-Path $OutputDirectory ($ScriptName + '.log')
    Write-Output "RUN $ScriptName"
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $godotCommandPath @arguments *> $logPath
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    $logLines = @(Get-Content -LiteralPath $logPath)
    $logLines | Write-Output
    if (($exitCode -ne 0) -or (Test-LogForEngineFailure -Lines $logLines)) {
        Add-Failure -Failures $Failures -Name $ScriptName -ExitCode $exitCode
    }
}

function Invoke-ScriptGroup {
    param(
        [string]$Name,
        [string[]]$Scripts,
        [System.Collections.Generic.List[string]]$Failures
    )

    Write-Output "GROUP $Name"
    foreach ($scriptName in $Scripts) {
        Invoke-GodotScript -ScriptName $scriptName -UserArguments @() -Failures $Failures
    }
}

function Invoke-ReplayGroup {
    param([System.Collections.Generic.List[string]]$Failures)

    if ($ReplayPath -eq '') {
        throw 'The replay group requires -ReplayPath.'
    }
    $resolvedReplayPath = [System.IO.Path]::GetFullPath($ReplayPath)
    if (-not (Test-Path -LiteralPath $resolvedReplayPath -PathType Leaf)) {
        throw "Replay file not found: $resolvedReplayPath"
    }
    Invoke-GodotScript -ScriptName 'replay_controller_smoke.gd' -UserArguments @(
        '--replay-smoke'
        $resolvedReplayPath
    ) -Failures $Failures
}

function Invoke-LobbyLoadGroup {
    param([System.Collections.Generic.List[string]]$Failures)

    if (-not (Test-Path -LiteralPath $lobbyLoadRunner -PathType Leaf)) {
        throw "Lobby load runner not found: $lobbyLoadRunner"
    }
    $arguments = @(
        '-NoProfile'
        '-File'
        $lobbyLoadRunner
        '-Clients'
        $LobbyClients
        '-GodotPath'
        $godotCommandPath
    )
    if ($TracksDirectory -ne '') {
        $arguments += '-TracksDirectory'
        $arguments += [System.IO.Path]::GetFullPath($TracksDirectory)
    }

    $logPath = Join-Path $OutputDirectory 'lobby-load.log'
    Write-Output 'RUN lobby-load'
    & (Get-Process -Id $PID).Path @arguments *> $logPath
    $exitCode = $LASTEXITCODE
    Get-Content -LiteralPath $logPath
    if ($exitCode -ne 0) {
        Add-Failure -Failures $Failures -Name 'lobby-load' -ExitCode $exitCode
    }
}

function Invoke-FailurePropagationSelfCheck {
    $shellPath = (Get-Process -Id $PID).Path
    & $shellPath -NoProfile -NonInteractive -Command 'exit 17'
    if ($LASTEXITCODE -ne 17) {
        throw "Failure propagation self-check expected exit 17, received $LASTEXITCODE."
    }
    Write-Output 'MXT_SMOKE_RUNNER_SELF_CHECK_PASS child_exit=17'
}

if ($Group -contains 'list') {
    Write-GroupList
    exit 0
}

if (-not (Test-Path -LiteralPath $GodotPath -PathType Leaf)) {
    Write-Error "Godot executable not found: $GodotPath"
    exit 2
}
if (-not (Test-Path -LiteralPath $godotCommandPath -PathType Leaf)) {
    Write-Error "Godot command executable not found: $godotCommandPath"
    exit 2
}
if (-not (Test-Path -LiteralPath $projectPath -PathType Container)) {
    Write-Error "Godot project directory not found: $projectPath"
    exit 2
}

if ($OutputDirectory -eq '') {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "mxt-smoke-suite-$stamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$failures = [System.Collections.Generic.List[string]]::new()
$requestedGroups = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($name in $Group) {
    [void]$requestedGroups.Add($name)
}

if ($requestedGroups.Contains('all-applicable')) {
    [void]$requestedGroups.Add('stable')
    [void]$requestedGroups.Add('simulation')
    if ($ReplayPath -ne '') {
        [void]$requestedGroups.Add('replay')
    } else {
        Write-Output 'SKIP replay: -ReplayPath was not supplied.'
    }
    if (Test-Path -LiteralPath $lobbyLoadRunner -PathType Leaf) {
        [void]$requestedGroups.Add('lobby-load')
    } else {
        Write-Output "SKIP lobby-load: helper not found at $lobbyLoadRunner"
    }
}

try {
    if ($requestedGroups.Contains('self-check')) {
        Invoke-FailurePropagationSelfCheck
    }
    if ($requestedGroups.Contains('stable')) {
        Invoke-ScriptGroup -Name 'stable' -Scripts $stableScripts -Failures $failures
    }
    if ($requestedGroups.Contains('simulation')) {
        Invoke-ScriptGroup -Name 'simulation' -Scripts $simulationScripts -Failures $failures
    }
    if ($requestedGroups.Contains('replay')) {
        Invoke-ReplayGroup -Failures $failures
    }
    if ($requestedGroups.Contains('lobby-load')) {
        Invoke-LobbyLoadGroup -Failures $failures
    }
}
catch {
    Write-Error $_
    exit 2
}

if ($failures.Count -gt 0) {
    Write-Error ('MXT_SMOKE_SUITE_FAIL ' + ($failures -join ', '))
    Write-Output "Logs: $OutputDirectory"
    exit 1
}

Write-Output "MXT_SMOKE_SUITE_PASS groups=$($Group -join ',')"
Write-Output "Logs: $OutputDirectory"
exit 0
