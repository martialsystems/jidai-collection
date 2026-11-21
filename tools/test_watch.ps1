# Copyright (c) 2026 Martial Systems LLC. All rights reserved.
#
# Rebuild the tree and run a ctest label tier whenever sources change (Windows PowerShell 5.1 or pwsh 7).
#
# Usage (from the repo root or anywhere):
#   powershell -ExecutionPolicy Bypass -File tools\test_watch.ps1
#   powershell -ExecutionPolicy Bypass -File tools\test_watch.ps1 -Once
#   ... -Label render | -Filter "smoke|rack" | -Preset windows-clangcl | -Jobs 2 | -DebounceMs 500
# Finds Visual Studio with vswhere and imports the x64 environment when cl.exe is not already on PATH.
# Uses watchexec when it is on PATH, otherwise a built-in FileSystemWatcher loop.
# CMakePresets live in jidai-rack/; the build directory is <repo>/build/<preset>.
param(
    [string]$Preset = "windows-dev",
    [string]$Label = "fast",
    [string]$Filter = "",
    [int]$Jobs = 2,
    [int]$DebounceMs = 300,
    [switch]$Once
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root
$Build = Join-Path (Join-Path $Root "build") $Preset

function Enter-VsEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    if ($Preset -like "*clangcl*" -and (Get-Command clang-cl.exe -ErrorAction SilentlyContinue)) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Visual Studio Build Tools not found. Install the 'Desktop development with C++' workload." }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "Visual Studio is installed but the MSVC x64/x86 build tools component is missing." }
    $dev = Join-Path $vs "Common7\Tools\Launch-VsDevShell.ps1"
    & $dev -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    Set-Location $Root
}

function Invoke-Cycle {
    Enter-VsEnvironment
    if (-not (Test-Path (Join-Path $Build "build.ninja"))) {
        & cmake -S (Join-Path $Root "jidai-rack") --preset $Preset | Out-Host
        if ($LASTEXITCODE -ne 0) { Write-Host "CONFIGURE FAILED" -ForegroundColor Red; return 1 }
    }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $targets = @("JidaiCommonTests", "JidaiDspTests", "JidaiHeaderHygiene", "JidaiCableEditTests", "JidaiRackTests", "JidaiPresetTests", "OrigamiTests")
    if ($Label -eq "render") { $targets += @("OrigamiPluginTests") }
    if ($Label -eq "slow") { $targets += @("JidaiRackProbe", "OrigamiPluginTests") }
    $buildArgs = @("--build", $Build, "--target") + $targets
    if ($Jobs -gt 0) { $buildArgs += @("--parallel", "$Jobs") }
    & cmake @buildArgs | Out-Host
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED" -ForegroundColor Red; return 1 }
    $tBuild = $sw.Elapsed.TotalSeconds; $sw.Restart()
    $ctestArgs = @("--test-dir", $Build, "-L", $Label, "--output-on-failure")
    if ($Jobs -gt 0) { $ctestArgs += @("--parallel", "$Jobs") }
    if ($Filter) { $ctestArgs += @("-R", $Filter) }
    & ctest @ctestArgs | Out-Host
    $rc = [int]$LASTEXITCODE
    $colour = "Green"; if ($rc -ne 0) { $colour = "Red" }
    Write-Host ("== build {0:N1} s, tests {1:N1} s, ctest exit {2} ({3})" -f $tBuild, $sw.Elapsed.TotalSeconds, $rc, (Get-Date -Format HH:mm:ss)) -ForegroundColor $colour
    return $rc
}

if ($Once) { exit (Invoke-Cycle) }

$null = Invoke-Cycle

$wx = Get-Command watchexec -ErrorAction SilentlyContinue
if ($wx) {
    Write-Host "watching jidai-common/, jidai-rack/, cmake/ with watchexec (Ctrl-C to stop)"
    $psExe = (Get-Process -Id $PID).Path
    $args2 = @("--watch", "jidai-common", "--watch", "jidai-rack", "--watch", "cmake",
               "--exts", "h,hh,hpp,hxx,c,cc,cpp,cxx,cmake,txt,json,xml,svg", "--debounce", "${DebounceMs}ms", "--clear", "-n", "-p", "--",
               $psExe, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $PSCommandPath, "-Once", "-Preset", $Preset, "-Label", $Label, "-Jobs", "$Jobs")
    if ($Filter) { $args2 += @("-Filter", $Filter) }
    & watchexec @args2
    exit $LASTEXITCODE
}

Write-Host "watchexec not found, using FileSystemWatcher (Ctrl-C to stop)."
$pattern = '(\.(h|hh|hpp|hxx|c|cc|cpp|cxx|cmake|json|xml|svg)$)|(CMakeLists\.txt$)|(CMakePresets\.json$)'
$watchers = @()
foreach ($d in @("jidai-common", "jidai-rack", "cmake")) {
    $path = Join-Path $Root $d
    if (-not (Test-Path $path)) { continue }
    $w = New-Object IO.FileSystemWatcher $path
    $w.IncludeSubdirectories = $true
    $w.NotifyFilter = [IO.NotifyFilters]'LastWrite, FileName, Size'
    $w.EnableRaisingEvents = $true
    foreach ($ev in @("Changed", "Created", "Renamed")) { $null = Register-ObjectEvent $w $ev -SourceIdentifier ("jidaiwatch_" + $d + "_" + $ev) }
    $watchers += $w
}
try {
    $last = $null
    while ($true) {
        $e = Wait-Event -Timeout 0.1
        if ($e) {
            Remove-Event -EventIdentifier $e.EventIdentifier
            if ($e.SourceEventArgs.FullPath -match $pattern) { $last = Get-Date }
        } elseif ($last -and ((Get-Date) - $last).TotalMilliseconds -ge $DebounceMs) {
            $last = $null
            Get-Event | Remove-Event
            Clear-Host
            $null = Invoke-Cycle
        }
    }
} finally {
    Get-EventSubscriber | Unregister-Event
    $watchers | ForEach-Object { $_.Dispose() }
}
