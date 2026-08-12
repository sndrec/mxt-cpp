param(
    [Parameter(Mandatory = $true)]
    [string]$Destination,
    [string]$GodotEditor = 'A:\programs\Godot_v4.7.1-stable_win64\Godot_v4.7.1-stable_win64_console.exe',
    [string]$ExportPreset = 'Windows Desktop',
    [string]$BaseContentDirectory = ''
)

$ErrorActionPreference = 'Stop'
$serviceRoot = $PSScriptRoot
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $serviceRoot '..\..')).Path
$projectRoot = Join-Path $repositoryRoot 'mxto'
$manifest = Join-Path $projectRoot 'steam\leaderboards.json'
$bundleMarkerName = '.mxt-verifier-bundle'
if ([string]::IsNullOrWhiteSpace($BaseContentDirectory)) {
    $BaseContentDirectory = Join-Path $repositoryRoot '..\steamworks-sdk\tools\ContentBuilder\content'
    if (-not (Test-Path -LiteralPath $BaseContentDirectory -PathType Container)) {
        $BaseContentDirectory = Join-Path $repositoryRoot '..\programming\steamworks-sdk\tools\ContentBuilder\content'
    }
}
$baseContentRoot = (Resolve-Path -LiteralPath $BaseContentDirectory).Path

foreach ($required in @($GodotEditor, (Join-Path $projectRoot 'bin\libgamesim.windows.template_release.x86_64.dll'), $manifest)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required verifier build file is missing: $required"
    }
}
$trackContentRoot = Join-Path $baseContentRoot 'track'
$vehicleContentRoot = Join-Path $baseContentRoot 'vehicle'
foreach ($requiredDirectory in @($trackContentRoot, $vehicleContentRoot)) {
    if (-not (Test-Path -LiteralPath $requiredDirectory -PathType Container)) {
        throw "Required base-content directory is missing: $requiredDirectory"
    }
}

$destinationRoot = [IO.Path]::GetFullPath($Destination)
$destinationParent = [IO.Path]::GetDirectoryName($destinationRoot)
if ($destinationRoot -eq [IO.Path]::GetPathRoot($destinationRoot) -or [string]::IsNullOrWhiteSpace($destinationParent)) {
    throw 'The bundle destination cannot be a drive root.'
}
if (Test-Path -LiteralPath $destinationRoot) {
    $existingEntries = @(Get-ChildItem -LiteralPath $destinationRoot -Force)
    if ($existingEntries.Count -gt 0 -and -not (Test-Path -LiteralPath (Join-Path $destinationRoot $bundleMarkerName) -PathType Leaf)) {
        throw 'The bundle destination is nonempty and was not created by this builder.'
    }
}

$stagingRoot = Join-Path $destinationParent ('.mxt-verifier-building-' + $PID)
$resolvedParentPrefix = $destinationParent.TrimEnd('\') + '\'
if (-not $stagingRoot.StartsWith($resolvedParentPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The staging directory escaped the bundle destination parent.'
}
if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}

$serviceDestination = Join-Path $stagingRoot 'service'
$runtimeDestination = Join-Path $stagingRoot 'runtime'
$configDestination = Join-Path $stagingRoot 'config'
New-Item -ItemType Directory -Path $serviceDestination, $runtimeDestination, $configDestination -Force | Out-Null
New-Item -ItemType File -Path (Join-Path $stagingRoot $bundleMarkerName) -Force | Out-Null

try {
    foreach ($name in @('server.py', 'replay_verifier.py', 'steam_web_api.py', 'README.md')) {
        Copy-Item -LiteralPath (Join-Path $serviceRoot $name) -Destination (Join-Path $serviceDestination $name) -Force
    }
    foreach ($name in @('start_windows_bundle.ps1', 'health_check.ps1', 'cloudflared.example.yml')) {
        Copy-Item -LiteralPath (Join-Path $serviceRoot $name) -Destination (Join-Path $stagingRoot $name) -Force
    }
    Copy-Item -LiteralPath $manifest -Destination (Join-Path $configDestination 'leaderboards.json') -Force

    $verifierExecutable = Join-Path $runtimeDestination 'MaxXThrottleVerifier.exe'
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $GodotEditor --headless --path $projectRoot --export-release $ExportPreset $verifierExecutable
        $exportExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exportExitCode -ne 0) {
        throw "Godot verifier export failed with exit code $exportExitCode."
    }
    $verifierPack = Join-Path $runtimeDestination 'MaxXThrottleVerifier.pck'
    if (-not (Test-Path -LiteralPath $verifierPack -PathType Leaf)) {
        $packCandidates = @(Get-ChildItem -LiteralPath $runtimeDestination -Filter '*.pck' -File)
        if ($packCandidates.Count -eq 1) {
            Move-Item -LiteralPath $packCandidates[0].FullName -Destination $verifierPack
        }
    }
    foreach ($requiredExport in @($verifierExecutable, $verifierPack)) {
        if (-not (Test-Path -LiteralPath $requiredExport -PathType Leaf)) {
            throw "Godot verifier export did not produce: $requiredExport"
        }
    }
    Copy-Item -LiteralPath $vehicleContentRoot -Destination (Join-Path $runtimeDestination 'vehicle') -Recurse -Force
    Copy-Item -LiteralPath $trackContentRoot -Destination (Join-Path $runtimeDestination 'track') -Recurse -Force

    $commit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
    $dirty = -not [string]::IsNullOrWhiteSpace((& git -C $repositoryRoot status --porcelain --untracked-files=no))
    $identityFiles = @(
        'runtime\MaxXThrottleVerifier.exe',
        'runtime\MaxXThrottleVerifier.pck',
        'runtime\libgamesim.windows.template_release.x86_64.dll',
        'runtime\vehicle\asset\accelerator\golden_fox.mxt_car_props',
        'runtime\track\Construction\track.mxt_track',
        'runtime\track\Construction\track.json',
        'config\leaderboards.json',
        'service\server.py',
        'service\replay_verifier.py',
        'service\steam_web_api.py'
    )
    $hashes = [ordered]@{}
    foreach ($relativePath in $identityFiles) {
        $hashes[$relativePath] = (Get-FileHash -LiteralPath (Join-Path $stagingRoot $relativePath) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    [ordered]@{
        format_revision = 1
        git_commit = $commit
        tracked_worktree_dirty = $dirty
        built_utc = [DateTime]::UtcNow.ToString('o')
        files_sha256 = $hashes
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $stagingRoot 'bundle_identity.json') -Encoding utf8

    if (Test-Path -LiteralPath $destinationRoot) {
        Remove-Item -LiteralPath $destinationRoot -Recurse -Force
    }
    Move-Item -LiteralPath $stagingRoot -Destination $destinationRoot

    Write-Host "Verifier bundle written to $destinationRoot"
    Write-Host "Git commit: $commit"
    if ($dirty) {
        Write-Warning 'Tracked files were dirty when the bundle was created; use a committed clean checkout for production.'
    }
} finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
}
