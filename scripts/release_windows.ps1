# Builds the Windows release: a portable zip and an installer.
#
# Replaces release_windows.cmd, which was written for Qt 5.13 and Visual Studio
# 2019 at hardcoded paths, copied OpenSSL 1.1.1 DLLs from a directory the
# builder was expected to have created by hand, shelled out to 7-Zip, and
# deployed a file called RcloneBrowser.exe -- a name this fork no longer
# builds. It also built 32-bit, which Qt 6 does not support.
#
# Everything comes from the environment now: Qt through CMAKE_PREFIX_PATH or
# windeployqt on PATH, the compiler through whatever the shell already has.
# That is what makes it run unchanged on a CI runner and on a developer's
# machine.
#
# Usage:  pwsh scripts/release_windows.ps1 [-Arch x64]
#
# NOTE: unlike the macOS and AppImage scripts, this one has never been run.
# There is no Windows machine here, so CI is its first execution.

param(
    [ValidateSet('x64', 'arm64')]
    [string]$Arch = 'x64'
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root 'build-release'
$Release = Join-Path $Root 'release'
$AppName = 'rclone-browser'

# A tagged build names itself after the tag; anything else carries the commit,
# which is what tells two development builds apart. Naming a *release* build
# after the commit is what let the 2.0.0 release end up offering three Windows
# zips: rebuilding the tag produced differently-named files, so uploading them
# added a set beside the old one instead of replacing it -- and one of the old
# ones did not run.
$Version = (Get-Content (Join-Path $Root 'VERSION') -Raw).Trim()
try {
    $Tag = (git -C $Root describe --exact-match --tags HEAD 2>$null)
    if ($LASTEXITCODE -eq 0 -and $Tag) {
        $Version = $Tag.Trim() -replace '^v', ''
    } else {
        $Commit = (git -C $Root rev-parse --short HEAD).Trim()
        if ($Commit) { $Version = "$Version-$Commit" }
    }
} catch {
    # A source archive with no git metadata is still buildable.
}

$StageName = "$AppName-$Version-windows-$Arch"
$Stage = Join-Path $Release $StageName

Write-Host "==> Building $AppName $Version for $Arch"
if (Test-Path $Build) { Remove-Item -Recurse -Force $Build }
cmake -S $Root -B $Build -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }

# The build output is echoed back as workflow annotations on failure, which
# can be read without signing in to GitHub -- the logs cannot. There is no
# Windows machine behind this script, so that is the only view of what broke.
$BuildLog = Join-Path $Build 'build.log'
cmake --build $Build --config Release 2>&1 | Tee-Object -FilePath $BuildLog
if ($LASTEXITCODE -ne 0) {
    Select-String -Path $BuildLog -Pattern 'error [A-Z]+[0-9]+' |
        Select-Object -First 15 |
        ForEach-Object { Write-Host "::error::$($_.Line.Trim())" }
    throw 'build failed'
}

# Single-config generators put it in build/, multi-config in build/Release.
$Exe = Get-ChildItem -Path $Build -Recurse -Filter "$AppName.exe" |
    Select-Object -First 1
if (-not $Exe) { throw "no $AppName.exe was produced" }

Write-Host "==> Staging into $StageName"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Path $Stage -Force | Out-Null

Copy-Item $Exe.FullName $Stage
Copy-Item (Join-Path $Root 'README.md') (Join-Path $Stage 'Readme.md')
Copy-Item (Join-Path $Root 'CHANGELOG.md') (Join-Path $Stage 'Changelog.md')
Copy-Item (Join-Path $Root 'LICENSE') (Join-Path $Stage 'License.txt')

Write-Host '==> Bundling Qt'
# --no-translations keeps the download small; the application has no
# translations of its own. The image format plugins stay: the icon cache reads
# whatever the shell hands it.
#
# --compiler-runtime, emphatically not --no-compiler-runtime, which is what
# this said at first: without the Visual C++ runtime DLLs beside the
# executable, the application starts only on a machine that already has the
# Visual C++ Redistributable installed. Developer machines do. A clean
# Windows 11 does not, and refuses to launch with four missing-DLL dialogs.
windeployqt --release --no-translations --compiler-runtime `
    (Join-Path $Stage "$AppName.exe")
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }

# windeployqt takes the runtime from the compiler environment, so if it was run
# outside a developer shell it quietly deploys nothing. Copy them directly in
# that case rather than shipping another broken zip.
$Runtime = @('msvcp140.dll', 'vcruntime140.dll')
if ($Arch -eq 'x64') { $Runtime += @('msvcp140_1.dll', 'vcruntime140_1.dll') }

if (($Runtime | Where-Object { -not (Test-Path (Join-Path $Stage $_)) }) -and
    $env:VCToolsRedistDir) {
    Write-Host '==> windeployqt left the runtime out; copying it from the toolchain'
    $CrtDir = Get-ChildItem -Path (Join-Path $env:VCToolsRedistDir $Arch) `
        -Filter 'Microsoft.VC*.CRT' -Directory -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($CrtDir) {
        Copy-Item (Join-Path $CrtDir.FullName '*.dll') $Stage -Force
    }
}

# windeployqt also drops the redistributable *installer* in beside the DLLs,
# which is 25MB of nothing useful: the DLLs next to the executable are what
# make it run, and a vc_redist.exe in the folder mostly invites someone to run
# it. It doubled the size of the download.
Get-ChildItem -Path $Stage -Filter 'vc_redist*.exe' -ErrorAction SilentlyContinue |
    ForEach-Object {
        Write-Host "==> Dropping $($_.Name), $([math]::Round($_.Length / 1MB)) MB"
        Remove-Item $_.FullName -Force
    }

Write-Host '==> Verifying the staged application'
foreach ($dll in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll')) {
    if (-not (Test-Path (Join-Path $Stage $dll))) {
        throw "$dll is missing from the staged application"
    }
}
if (-not (Test-Path (Join-Path $Stage 'platforms\qwindows.dll'))) {
    throw 'the Windows platform plugin is missing; the application would not start'
}

# The check that would have caught this before anyone downloaded it. There is
# no Windows machine here to launch the result on, so the build failing is the
# only thing standing between a missing DLL and a release nobody can run.
$MissingRuntime = $Runtime | Where-Object { -not (Test-Path (Join-Path $Stage $_)) }
if ($MissingRuntime) {
    throw ("the Visual C++ runtime is missing from the staged application: " +
           ($MissingRuntime -join ', ') +
           ". It would not start on a machine without the Visual C++ " +
           "Redistributable installed.")
}

# Reported as a workflow notice, which is readable without a GitHub login --
# the logs are not. It is the only way to see what actually got staged from a
# machine that cannot run any of this.
$Staged = (Get-ChildItem -Path $Stage -File | Measure-Object).Count
$StagedMB = [math]::Round((Get-ChildItem -Path $Stage -Recurse -File |
    Measure-Object -Property Length -Sum).Sum / 1MB)
Write-Host ("::notice::staged $Staged files, $StagedMB MB, runtime: " +
            ($Runtime -join ', '))

Write-Host '==> Zipping'
$Zip = Join-Path $Release "$StageName.zip"
if (Test-Path $Zip) { Remove-Item -Force $Zip }
Compress-Archive -Path $Stage -DestinationPath $Zip

# Inno Setup is not part of a normal Qt install, so the installer is optional:
# the zip above is a complete, portable copy either way.
$Iscc = Get-Command iscc.exe -ErrorAction SilentlyContinue
if (-not $Iscc) {
    $Candidate = 'C:\Program Files (x86)\Inno Setup 6\iscc.exe'
    if (Test-Path $Candidate) { $Iscc = $Candidate } else { $Iscc = $null }
} else {
    $Iscc = $Iscc.Source
}

if ($Iscc) {
    Write-Host '==> Building the installer'
    & $Iscc `
        "/dMyAppVersion=$((Get-Content (Join-Path $Root 'VERSION') -Raw).Trim())" `
        "/dMyAppDir=$StageName" `
        "/dMyAppArch=$Arch" `
        "/O$Release" `
        "/F$StageName-setup" `
        (Join-Path $PSScriptRoot 'rclone-browser-win-installer.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed' }
    Write-Host "Done: $Release\$StageName-setup.exe"
} else {
    Write-Host 'Inno Setup not found, so no installer was built (the zip is complete).'
}

Write-Host "Done: $Zip"
