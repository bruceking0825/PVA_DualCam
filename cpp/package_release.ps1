# Run with Windows PowerShell 5.1 or PowerShell 7. No administrator rights needed.
# Uses the current Release CMake cache, rather than machine-specific Qt/OpenCV paths.
[CmdletBinding()]
param(
    [string]$BuildDirectory,
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
        $BuildDirectory = Join-Path $projectDirectory 'out\build\tasks-release'
    }
    $BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
    $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
    Require-File $cachePath
    $script:cacheLines = Get-Content -LiteralPath $cachePath
    if ((Read-CacheValue 'CMAKE_BUILD_TYPE') -ne 'Release') {
        throw 'This script requires a Release build directory (not Debug).'
    }

    $exePath = Join-Path $BuildDirectory 'SherlockSrvr.exe'
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
        $folderName = 'PVA_DualCam_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '_' + [guid]::NewGuid().ToString('N').Substring(0, 6)
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
        if ($graphPath) {
            Copy-Item -LiteralPath $graphPath -Destination (Join-Path $packageDirectory 'graph.json')
        }

        # Invoke the tool from this build's Qt installation. OpenCV was copied separately.
        $originalPath = $env:PATH
        try {
            $env:PATH = $qtBin + ';' + $opencvBin + ';' + $originalPath
            & $deployTool --release --no-compiler-runtime (Join-Path $packageDirectory 'SherlockSrvr.exe')
            if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
        }
        finally { $env:PATH = $originalPath }
        Require-File (Join-Path $packageDirectory 'platforms\qwindows.dll')

        New-Item -ItemType Directory -Path (Join-Path $packageDirectory 'pva_img\pair') -Force | Out-Null
        $notes = @'
PVA DualCam - Release deployment

1. Copy this ENTIRE folder to a writable location on the target computer.
2. Install the Microsoft Visual C++ v14 Redistributable matching the build
   architecture (the current project uses x64). Visual Studio is not required.
   https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/
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
        Write-Host 'Install the matching Visual C++ runtime on the target PC. Check cnf.ini paths.'
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
