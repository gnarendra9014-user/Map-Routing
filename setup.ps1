# setup.ps1 - Self-contained setup: finds VS2022 tools, installs vcpkg & deps, builds project
# Run from any PowerShell window, even with a stripped PATH
# Usage: & "C:\Users\nanin\OneDrive\Desktop\map routing\setup.ps1"

$ErrorActionPreference = "Stop"
$ProjectDir = "C:\Users\nanin\OneDrive\Desktop\map routing"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " Map Routing Engine - Self-Contained Setup" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# === Step 1: Find Visual Studio via vswhere (fixed path, always present) ======
Write-Host "`n[1/6] Locating Visual Studio 2022..." -ForegroundColor Yellow

$VsWhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $VsWhere)) {
    Write-Error @"
vswhere.exe not found at expected path. This usually means Visual Studio
is not installed. Please install Visual Studio 2022 with the
'Desktop development with C++' workload from:
https://visualstudio.microsoft.com/downloads/
"@
    exit 1
}

# Get VS install path
$VsPath = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
if (-not $VsPath) {
    $VsPath = & $VsWhere -latest -products * -property installationPath 2>$null
}
if (-not $VsPath) {
    Write-Error "Visual Studio not found via vswhere. Is the 'Desktop development with C++' workload installed?"
    exit 1
}

Write-Host "  Found VS at: $VsPath" -ForegroundColor Green

# === Step 2: Find CMake bundled with VS =======================================
Write-Host "`n[2/6] Locating CMake..." -ForegroundColor Yellow

# VS bundles cmake at a known relative path
$CmakeVsPath = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$CmakeSystem = "C:\Program Files\CMake\bin\cmake.exe"
$CmakeChoco  = "C:\ProgramData\chocolatey\bin\cmake.exe"

$CmakeExe = @($CmakeVsPath, $CmakeSystem, $CmakeChoco) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $CmakeExe) {
    Write-Host "  CMake not found bundled with VS. Downloading portable CMake..." -ForegroundColor Yellow
    $CmakeZip  = "$env:TEMP\cmake-portable.zip"
    $CmakeDir  = "C:\cmake-portable"
    Invoke-WebRequest -Uri "https://github.com/Kitware/CMake/releases/download/v3.29.3/cmake-3.29.3-windows-x86_64.zip" `
                      -OutFile $CmakeZip -UseBasicParsing
    Expand-Archive -Path $CmakeZip -DestinationPath "C:\" -Force
    $CmakeExe = (Get-ChildItem "C:\cmake-*\bin\cmake.exe" | Select-Object -First 1).FullName
}

if (-not $CmakeExe) { Write-Error "Could not find or install CMake"; exit 1 }
Write-Host "  CMake: $CmakeExe" -ForegroundColor Green

# === Step 3: Find MSVC compiler (cl.exe) ======================================
Write-Host "`n[3/6] Locating MSVC compiler (cl.exe)..." -ForegroundColor Yellow

$VcToolsBase = Join-Path $VsPath "VC\Tools\MSVC"
$ClExe = Get-ChildItem "$VcToolsBase\*\bin\Hostx64\x64\cl.exe" -ErrorAction SilentlyContinue |
         Sort-Object FullName -Descending | Select-Object -First 1

if (-not $ClExe) {
    Write-Error @"
cl.exe not found under $VcToolsBase
Please install the 'Desktop development with C++' workload in Visual Studio Installer.
"@
    exit 1
}
Write-Host "  MSVC cl.exe: $($ClExe.FullName)" -ForegroundColor Green

# Derive the VC tools version and Ninja generator path
$VcToolsVersion = $ClExe.FullName | Split-Path -Parent | Split-Path -Parent | Split-Path -Parent | Split-Path -Leaf
Write-Host "  VC Tools version: $VcToolsVersion" -ForegroundColor DarkGray

# === Step 4: Find Git bundled with VS (optional, for vcpkg) ===================
Write-Host "`n[4/6] Locating Git..." -ForegroundColor Yellow

$GitVsPath = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe"
$GitPaths = @(
    $GitVsPath,
    "C:\Program Files\Git\cmd\git.exe",
    "C:\Program Files (x86)\Git\cmd\git.exe"
)
$GitExe = $GitPaths | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($GitExe) {
    Write-Host "  Git: $GitExe" -ForegroundColor Green
} else {
    Write-Host "  Git not found - will download vcpkg as zip instead" -ForegroundColor Yellow
}

# === Step 5: Install vcpkg ====================================================
Write-Host "`n[5/6] Setting up vcpkg..." -ForegroundColor Yellow

$VcpkgRoot = "C:\vcpkg"
$VcpkgExe  = Join-Path $VcpkgRoot "vcpkg.exe"

if (-not (Test-Path $VcpkgExe)) {
    if ($GitExe) {
        Write-Host "  Cloning vcpkg with git..." -ForegroundColor DarkGray
        & $GitExe clone https://github.com/microsoft/vcpkg $VcpkgRoot 2>&1
        & (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    } else {
        Write-Host "  Downloading vcpkg as zip (no git needed)..." -ForegroundColor DarkGray
        $VcpkgZip = "$env:TEMP\vcpkg.zip"
        Invoke-WebRequest -Uri "https://github.com/microsoft/vcpkg/archive/refs/heads/master.zip" `
                          -OutFile $VcpkgZip -UseBasicParsing
        Write-Host "  Extracting vcpkg..." -ForegroundColor DarkGray
        Expand-Archive -Path $VcpkgZip -DestinationPath "C:\" -Force
        if (Test-Path "C:\vcpkg-master") {
            Rename-Item "C:\vcpkg-master" "vcpkg" -Force -ErrorAction SilentlyContinue
        }
        # Bootstrap
        Write-Host "  Bootstrapping vcpkg..." -ForegroundColor DarkGray
        & (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    }
}

if (-not (Test-Path $VcpkgExe)) {
    Write-Error "vcpkg.exe not found after setup at $VcpkgRoot"
    exit 1
}
Write-Host "  vcpkg: $VcpkgExe" -ForegroundColor Green

# Install dependencies
Write-Host "  Installing libosmium, protozero, zlib, bzip2 (this may take 5-10 min first time)..." -ForegroundColor DarkGray
& $VcpkgExe install --triplet=x64-windows 2>&1 | Tee-Object -Variable vcpkgOut
Write-Host "  vcpkg install complete." -ForegroundColor Green

# === Step 6: Configure and Build ==============================================
Write-Host "`n[6/6] Configuring and building the project..." -ForegroundColor Yellow

$BuildDir    = Join-Path $ProjectDir "build"
$VcpkgToolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

# Create data directory
New-Item -ItemType Directory -Force -Path (Join-Path $ProjectDir "data") | Out-Null

Write-Host "  Running CMake configure..." -ForegroundColor DarkGray
& "$CmakeExe" -S "$ProjectDir" -B "$BuildDir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VcpkgToolchain" -DVCPKG_TARGET_TRIPLET=x64-windows

if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed."
    exit 1
}

Write-Host "  Running CMake build..." -ForegroundColor DarkGray
& "$CmakeExe" --build "$BuildDir" --config Release --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed. See output above."
    exit 1
}

# === Done =====================================================================
$Exe = Join-Path $BuildDir "Release\map_routing.exe"
if (Test-Path $Exe) {
    Write-Host "`n============================================================" -ForegroundColor Green
    Write-Host " BUILD SUCCESSFUL" -ForegroundColor Green
    Write-Host "============================================================" -ForegroundColor Green
    Write-Host " Executable: $Exe" -ForegroundColor Green
    Write-Host @"

 Next steps:
   1. Download Monaco OSM (4.5 MB):
      Invoke-WebRequest "https://download.geofabrik.de/europe/monaco-latest.osm.pbf" -OutFile "$ProjectDir\data\monaco.osm.pbf"

   2. Parse OSM -> graph:
      & "$Exe" parse "$ProjectDir\data\monaco.osm.pbf" "$ProjectDir\data\monaco.graph"

   3. Build CH index:
      & "$Exe" build "$ProjectDir\data\monaco.graph" "$ProjectDir\data\monaco.ch"

   4. Route query:
      & "$Exe" route "$ProjectDir\data\monaco.graph" --ch "$ProjectDir\data\monaco.ch" --src 0 --dst 1000 --algo ch --out "$ProjectDir\data\route.geojson"

   5. Open route.geojson at https://geojson.io
"@ -ForegroundColor Cyan
} else {
    Write-Host " Executable not found at expected path. Check build output above." -ForegroundColor Red
}
