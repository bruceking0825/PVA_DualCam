# Run with Windows PowerShell 5.1 or PowerShell 7. No administrator rights needed.
# Uses the current Release CMake cache, rather than machine-specific Qt/OpenCV paths.
[CmdletBinding()]
param(
    [string]$BuildDirectory,
    [string]$VcRuntimeDirectory,
    [switch]$ValidateOnly,
    [switch]$NoPause
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$exitCode = 0
$packageDirectory = $null

function Require-File([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file not found: $Path"
    }
}

function Read-CacheValue([string]$Name) {
    $pattern = '^' + [regex]::Escape($Name) + ':[^=]+=(.*)$'
    foreach ($line in $script:cacheLines) {
        if ($line -match $pattern) { return $Matches[1] }
    }
    throw "Missing $Name in CMakeCache.txt. Configure and build Release first."
}

try {
    $projectDirectory = Split-Path -Parent $PSScriptRoot
    if (-not $BuildDirectory) {
        $BuildDirectory = Join-Path $projectDirectory 'build-win32-release'
    }
    $BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
    $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
    Require-File $cachePath
    $script:cacheLines = Get-Content -LiteralPath $cachePath
    if ((Read-CacheValue 'CMAKE_BUILD_TYPE') -ne 'Release') {
        throw 'This script requires a Release build directory (not Debug).'
    }

    $exePath = Join-Path $BuildDirectory 'pva_dualcam_cpp.exe'
    $configPath = Join-Path $projectDirectory 'cnf.ini'
    $themeDirectory = Join-Path $projectDirectory 'themes'
    Require-File $exePath
    Require-File $configPath
    Require-File (Join-Path $themeDirectory 'py_dracula_dark.qss')

    # Qt6_DIR points to <Qt>/lib/cmake/Qt6; fall back to Qt5 builds.
    $qt6Entry = $script:cacheLines | Where-Object { $_ -match '^Qt6_DIR:[^=]+=' } | Select-Object -First 1
    $qtKey = if ($qt6Entry -and $qt6Entry -notmatch 'NOTFOUND$') { 'Qt6_DIR' } else { 'Qt5_DIR' }
    $qtConfigDirectory = Read-CacheValue $qtKey
    $qtRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qtConfigDirectory))
    $qtBin = Join-Path $qtRoot 'bin'
    $deployTool = Join-Path $qtBin 'windeployqt.exe'
    Require-File $deployTool

    $opencvDirectory = Read-CacheValue 'OpenCV_DIR'
    $opencvBin = Join-Path (Split-Path -Parent $opencvDirectory) 'bin'
    $opencvDlls = @(Get-ChildItem -LiteralPath $opencvBin -Filter 'opencv_*.dll' |
        Where-Object { $_.Name -notmatch 'd\.dll$' })
    if (-not ($opencvDlls | Where-Object { $_.Name -match '^opencv_(world|core)\d+\.dll$' })) {
        throw "Release OpenCV DLLs not found in $opencvBin"
    }

    # Check the EXE and source DLLs before creating any output directory.
    function Assert-X86([string]$Path) {
        $stream = [System.IO.File]::OpenRead($Path)
        $reader = New-Object System.IO.BinaryReader($stream)
        try {
            if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Invalid PE file: $Path" }
            $stream.Position = 0x3C
            $offset = $reader.ReadInt32()
            if ($offset -lt 64 -or $offset -gt ($stream.Length - 6)) { throw "Invalid PE header: $Path" }
            $stream.Position = $offset
            if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x014C) {
                throw "Expected an x86 (32-bit) PE file: $Path"
            }
        }
        finally { $reader.Dispose(); $stream.Dispose() }
    }
    Assert-X86 $exePath
    $qtMajor = if ($qtKey -eq 'Qt6_DIR') { '6' } else { '5' }
    Assert-X86 (Join-Path $qtBin "Qt${qtMajor}Core.dll")
    foreach ($dll in $opencvDlls) { Assert-X86 $dll.FullName }

    # 使用 VS Redist 中可随应用部署的 CRT，不从 Windows 系统目录复制。
    if (-not $VcRuntimeDirectory) {
        $compiler = Read-CacheValue 'CMAKE_CXX_COMPILER'
        $vcRoot = $compiler -replace '[/\\]Tools[/\\]MSVC[/\\].*$', ''
        $redistRoot = Join-Path $vcRoot 'Redist\MSVC'
        $candidates = @(Get-ChildItem -LiteralPath $redistRoot -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending)
        foreach ($candidate in $candidates) {
            $x86Root = Join-Path $candidate.FullName 'x86'
            if (-not (Test-Path -LiteralPath $x86Root -PathType Container)) { continue }
            $crt = Get-ChildItem -LiteralPath $x86Root -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
            if ($crt) { $VcRuntimeDirectory = $crt.FullName; break }
        }
    }
    if (-not $VcRuntimeDirectory) { throw 'x86 VC runtime not found. Pass -VcRuntimeDirectory pointing to VS Redist x86 Microsoft.VC*.CRT.' }
    Require-File (Join-Path $VcRuntimeDirectory 'msvcp140.dll')
    Require-File (Join-Path $VcRuntimeDirectory 'vcruntime140.dll')
    $vcRuntimeDlls = @(Get-ChildItem -LiteralPath $VcRuntimeDirectory -Filter '*.dll')
    foreach ($dll in $vcRuntimeDlls) { Assert-X86 $dll.FullName }
    Write-Host "x86 VC runtime:     $VcRuntimeDirectory"

    $graphPath = @(
        (Join-Path $projectDirectory 'src\graph.json'),
        (Join-Path $projectDirectory 'graph.json')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1

    Write-Host "Release executable: $exePath"
    Write-Host "Qt deployment tool: $deployTool"
    Write-Host "OpenCV DLL folder:  $opencvBin"
    if (-not $graphPath) {
        Write-Warning 'graph.json is absent; the optional camera preview pipeline will not be included.'
    }
    if ($ValidateOnly) {
        Write-Host 'Validation passed. No files copied.' -ForegroundColor Green
    }
    else {
        # Every run gets a new folder. Existing packages and user state are never overwritten.
        $outputRoot = Join-Path $projectDirectory 'dist'
        $folderName = 'PVA_DualCam_x86_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '_' + [guid]::NewGuid().ToString('N').Substring(0, 6)
        $packageDirectory = Join-Path $outputRoot $folderName
        New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null
        $incompleteMarker = Join-Path $packageDirectory 'PACKAGING_INCOMPLETE.txt'
        Set-Content -LiteralPath $incompleteMarker -Value 'Packaging did not complete. Do not distribute this folder.' -Encoding UTF8

        Copy-Item -LiteralPath $exePath -Destination $packageDirectory
        Copy-Item -LiteralPath $configPath -Destination $packageDirectory
        Copy-Item -LiteralPath $themeDirectory -Destination (Join-Path $packageDirectory 'themes') -Recurse
        foreach ($dll in $opencvDlls) {
            Copy-Item -LiteralPath $dll.FullName -Destination $packageDirectory
        }
        foreach ($dll in $vcRuntimeDlls) {
            Copy-Item -LiteralPath $dll.FullName -Destination $packageDirectory
        }
        # 固定工作目录，快捷方式或从其他目录启动时仍读取包内配置。
        $launcher = @"
@echo off
cd /d "%~dp0"
start "" "%~dp0pva_dualcam_cpp.exe"
"@
        Set-Content -LiteralPath (Join-Path $packageDirectory 'start.cmd') -Value $launcher -Encoding ascii

        if ($graphPath) {
            Copy-Item -LiteralPath $graphPath -Destination (Join-Path $packageDirectory 'graph.json')
        }

        # Invoke the tool from this build's Qt installation. OpenCV was copied separately.
        $originalPath = $env:PATH
        try {
            $env:PATH = $qtBin + ';' + $opencvBin + ';' + $originalPath
            & $deployTool --release --no-compiler-runtime (Join-Path $packageDirectory 'pva_dualcam_cpp.exe')
            if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
        }
        finally { $env:PATH = $originalPath }
        Require-File (Join-Path $packageDirectory 'platforms\qwindows.dll')

        New-Item -ItemType Directory -Path (Join-Path $packageDirectory 'pva_img\pair') -Force | Out-Null
        foreach ($dll in (Get-ChildItem -LiteralPath $packageDirectory -Filter '*.dll' -Recurse)) {
            Assert-X86 $dll.FullName
        }

        $notes = @'
PVA DualCam - Release deployment

1. Copy this ENTIRE folder to a writable location on the target computer.
2. x86 Visual C++ runtime DLLs are included beside the EXE. On supported
   Windows 10/11, no separate VC runtime installation is needed.
   Launch start.cmd. Visual Studio, Qt SDK and OpenCV SDK are not required.
3. cnf.ini is copied unchanged. Check offline_image_dir, facette_image_dir,
   state_file and camera settings for the target computer.
4. Offline images and existing measurement/PLC/camera state are NOT copied.
   Put offline pairs in pva_img/pair or the directory configured in cnf.ini.
5. For online cameras, the EXE must have been built with Sapera support.
   Install the matching Sapera runtime and device drivers on the target PC.
   This script does not install or copy Sapera. Copying DLLs cannot enable
   camera support that was disabled at compile time.
6. Test startup, measurement and saving on a computer without development
   tools before distributing this folder. Qt plugin subfolders must stay intact.
'@
        Set-Content -LiteralPath (Join-Path $packageDirectory 'DEPLOYMENT_README.txt') -Value $notes -Encoding UTF8
        Remove-Item -LiteralPath $incompleteMarker
        Write-Host "`nPackage ready: $packageDirectory" -ForegroundColor Green
        Write-Host 'x86 VC runtime included. Launch start.cmd; check cnf.ini paths.'
        Write-Warning 'Online cameras require a Sapera-enabled build and the target PC camera runtime/drivers.'
    }
}
catch {
    $exitCode = 1
    Write-Host "`nPackaging failed: $($_.Exception.Message)" -ForegroundColor Red
    if ($packageDirectory) { Write-Host "Incomplete output retained for diagnosis: $packageDirectory" }
}
finally {
    if (-not $NoPause) { Read-Host 'Press Enter to close' | Out-Null }
}
exit $exitCode


