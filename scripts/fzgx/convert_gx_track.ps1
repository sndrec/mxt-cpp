param(
    [Parameter(Mandatory=$true)]
    [string]$CoursePath,

    [string]$OutDir = "A:\blender_stuff\mxt\mxt_track_editor\tracks\fzgx_imports",
    [double]$Step = 8.0,
    [int]$CourseId = -1,
    [string]$TrackName = "",
    [string]$BlenderExe = "A:\blender_stuff\mxt\mxt_track_editor\build_windows_x64_vc17_Release\bin\Release\blender.exe",
    [string]$FzgxRoot = "A:\programs\smb1-decomp\src-fzgx"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$SamplerSource = Join-Path $ScriptDir "gx_track_sampler.cpp"
$SamplerExe = Join-Path $ScriptDir "gx_track_sampler.exe"
$ContentInclude = Join-Path $FzgxRoot "content_c\include"
$ContentLib = Join-Path $FzgxRoot "build-fzgx-win-native\content_c\libfzgx_content.a"
$BlenderScript = Join-Path $ScriptDir "gx_samples_to_mxt_blend.py"

if ($CourseId -lt 0) {
    $CourseFile = Split-Path -Leaf $CoursePath
    if ($CourseFile -match "COLI_COURSE(\d+)") {
        $CourseId = [int]$Matches[1]
    } else {
        throw "Could not infer course id from $CourseFile; pass -CourseId."
    }
}
if ([string]::IsNullOrWhiteSpace($TrackName)) {
    $TrackName = "FZGX_COURSE{0:D2}" -f $CourseId
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
$Base = Join-Path $OutDir ("fzgx_course{0:D2}" -f $CourseId)
$SamplesJson = "$Base`_samples.json"
$BlendOut = "$Base.blend"
$TrackOut = "$Base.mxt_track"

Push-Location $RepoRoot
try {
    & g++ -std=c++17 -O2 "-I$ContentInclude" $SamplerSource $ContentLib -o $SamplerExe -lm
    if ($LASTEXITCODE -ne 0) { throw "sampler compile failed" }

    & $SamplerExe $CoursePath $SamplesJson --step $Step --course-id $CourseId
    if ($LASTEXITCODE -ne 0) { throw "sampler run failed" }

    & $BlenderExe --background --factory-startup --python $BlenderScript -- $SamplesJson $BlendOut --export $TrackOut --track-name $TrackName
    if ($LASTEXITCODE -ne 0) { throw "Blender conversion failed" }
    $BlendBackup = "$BlendOut`1"
    if (Test-Path -LiteralPath $BlendBackup) {
        Remove-Item -LiteralPath $BlendBackup -Force
    }
} finally {
    Pop-Location
}

Write-Host "Wrote:"
Write-Host "  $SamplesJson"
Write-Host "  $BlendOut"
Write-Host "  $TrackOut"
