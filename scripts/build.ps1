# Builds DivaStudio from a plain PowerShell window:
# loads the MSVC x64 environment, finds Qt and FFmpeg, then runs the CMake preset.
#   .\scripts\build.ps1            # RelWithDebInfo
#   .\scripts\build.ps1 -Preset debug
param([string]$Preset = "release")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -version "[17.0,)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio 2022+ with the C++ tools was not found." }
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($matches[1])" $matches[2] }
}

# pip puts cmake/ninja in the per-user Scripts folder, which is often not on PATH.
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $scripts = python -c "import sysconfig; print(sysconfig.get_path('scripts', 'nt_user'))"
    $env:PATH = "$scripts;$env:PATH"
}

if (-not $env:QTDIR) {
    $qt = Get-ChildItem "C:\Qt\6.*\msvc2022_64" -Directory -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $qt) { throw "Qt 6 (msvc2022_64) not found; set QTDIR." }
    $env:QTDIR = $qt.FullName
}
if (-not $env:FFMPEG_ROOT) {
    $ff = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BtbN.FFmpeg.GPL.Shared.*\ffmpeg-*" -Directory -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $ff) { throw "FFmpeg dev package not found; set FFMPEG_ROOT." }
    $env:FFMPEG_ROOT = $ff.FullName
}

Push-Location $root
try {
    cmake --preset $Preset
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    cmake --build --preset $Preset
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
