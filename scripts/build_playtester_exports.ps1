param(
    [switch]$Run,
    [string[]]$Platforms = @("Windows", "Linux", "macOS"),
    [switch]$BuildOnly,
    [switch]$ExportOnly,
    [string]$GodotExe = "A:\programs\Godot_v4.6.1-stable_win64\Godot_v4.6.1-stable_win64.exe",
    [string]$WslDistro = "Ubuntu"
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ProjectDir = Join-Path $Root "mxto"
$ExportDir = Join-Path $Root "export-bin"

function Convert-ToWslPath {
    param([string]$Path)

    $resolved = (Resolve-Path $Path).Path
    $drive = $resolved.Substring(0, 1).ToLowerInvariant()
    $rest = $resolved.Substring(2).Replace("\", "/")
    return "/mnt/$drive$rest"
}

function Invoke-Step {
    param(
        [string]$Title,
        [string]$CommandText,
        [scriptblock]$Action
    )

    Write-Host ""
    Write-Host "== $Title =="
    Write-Host $CommandText
    if ($Run) {
        & $Action
    }
}

function Invoke-WslBash {
    param([string]$Command)

    $wslRoot = Convert-ToWslPath $Root
    & wsl.exe -d $WslDistro --cd $wslRoot -- bash -lc $Command
    if ($LASTEXITCODE -ne 0) {
        throw "WSL command failed with exit code $LASTEXITCODE"
    }
}

function Test-PlatformSelected {
    param([string]$Name)
    return $Platforms | Where-Object { $_.Equals($Name, [System.StringComparison]::OrdinalIgnoreCase) }
}

New-Item -ItemType Directory -Force -Path $ExportDir | Out-Null

Write-Host "Root: $Root"
Write-Host "Godot: $GodotExe"
Write-Host "Mode: $(if ($Run) { 'RUN' } else { 'DRY RUN' })"

if (-not $Run) {
    Write-Host ""
    Write-Host "Dry run only. Re-run with -Run to actually build/export."
}

if (-not $ExportOnly) {
    if (Test-PlatformSelected "Windows") {
        Invoke-Step "Build Windows GDExtension" `
            "scons platform=windows target=template_release arch=x86_64 -j4" `
            { Push-Location $Root; try { & scons platform=windows target=template_release arch=x86_64 -j4 } finally { Pop-Location } }
    }

    if (Test-PlatformSelected "Linux") {
        $linuxCommand = @"
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
command -v scons >/dev/null || { echo 'Missing native WSL scons. Run: sudo apt update && sudo apt install -y build-essential scons python3 pkg-config'; exit 127; }
scons platform=linux target=template_release arch=x86_64 -j4
"@.Trim()
        Invoke-Step "Build Linux GDExtension in WSL" `
            "wsl.exe -d $WslDistro --cd $(Convert-ToWslPath $Root) -- bash -lc '$linuxCommand'" `
            { Invoke-WslBash $linuxCommand }
    }

    if (Test-PlatformSelected "macOS") {
        $macCommand = @"
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
test -n "`$OSXCROSS_ROOT" || { echo 'macOS build requires either a real Mac or OSXCROSS_ROOT configured in WSL.'; exit 125; }
command -v scons >/dev/null || { echo 'Missing native WSL scons. Run: sudo apt update && sudo apt install -y build-essential scons python3 pkg-config'; exit 127; }
scons platform=macos target=template_release arch=universal -j4
"@.Trim()
        Invoke-Step "Build macOS GDExtension with osxcross in WSL" `
            "wsl.exe -d $WslDistro --cd $(Convert-ToWslPath $Root) -- bash -lc '$macCommand'" `
            { Invoke-WslBash $macCommand }
    }
}

if (-not $BuildOnly) {
    if (-not (Test-Path $GodotExe)) {
        throw "Godot executable not found: $GodotExe"
    }

    if (Test-PlatformSelected "Windows") {
        $outPath = Join-Path $ExportDir "Maxx Throttle.exe"
        Invoke-Step "Export Windows" `
            "`"$GodotExe`" --headless --path `"$ProjectDir`" --export-release `"Windows Desktop`" `"$outPath`"" `
            { & $GodotExe --headless --path $ProjectDir --export-release "Windows Desktop" $outPath }
    }

    if (Test-PlatformSelected "Linux") {
        $outPath = Join-Path $ExportDir "Maxx Throttle.x86_64"
        Invoke-Step "Export Linux" `
            "`"$GodotExe`" --headless --path `"$ProjectDir`" --export-release `"Linux`" `"$outPath`"" `
            { & $GodotExe --headless --path $ProjectDir --export-release "Linux" $outPath }
    }

    if (Test-PlatformSelected "macOS") {
        $outPath = Join-Path $ExportDir "Maxx Throttle macOS.zip"
        Invoke-Step "Export macOS" `
            "`"$GodotExe`" --headless --path `"$ProjectDir`" --export-release `"macOS`" `"$outPath`"" `
            { & $GodotExe --headless --path $ProjectDir --export-release "macOS" $outPath }
    }
}
