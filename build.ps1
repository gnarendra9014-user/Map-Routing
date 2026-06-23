# Build and run the map routing engine (PowerShell)
# Run this from the map-routing directory

param(
    [string]$BuildType = "Release",
    [switch]$RunTests,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ProjectDir = $PSScriptRoot
$BuildDir   = Join-Path $ProjectDir "build"

# === Check vcpkg ===============================================================
$VcpkgRoot = $env:VCPKG_ROOT
if (-not $VcpkgRoot) {
    # Try common locations
    $candidates = @(
        "C:\vcpkg",
        "C:\tools\vcpkg",
        "$env:USERPROFILE\vcpkg",
        "$env:LOCALAPPDATA\vcpkg"
    )
    foreach ($c in $candidates) {
        if (Test-Path "$c\vcpkg.exe") { $VcpkgRoot = $c; break }
    }
}

if (-not $VcpkgRoot) {
    Write-Error @"
vcpkg not found. Install it first:
    git clone https://github.com/microsoft/vcpkg C:\vcpkg
    C:\vcpkg\bootstrap-vcpkg.bat
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', 'C:\vcpkg', 'User')
"@
    exit 1
}

Write-Host "Using vcpkg at: $VcpkgRoot" -ForegroundColor Cyan

# === Install dependencies =======================================================
Write-Host "Installing dependencies via vcpkg..." -ForegroundColor Cyan
& "$VcpkgRoot\vcpkg.exe" install --triplet x64-windows | Out-Host

# === Clean =====================================================================
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# === Configure =================================================================
Write-Host "Configuring CMake ($BuildType)..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force $BuildDir | Out-Null

$vcpkgToolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

cmake -S $ProjectDir -B $BuildDir `
    -DCMAKE_BUILD_TYPE=$BuildType `
    -DCMAKE_TOOLCHAIN_FILE="$vcpkgToolchain" `
    -DVCPKG_TARGET_TRIPLET=x64-windows

if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed"; exit 1 }

# === Build =====================================================================
Write-Host "Building..." -ForegroundColor Cyan
cmake --build $BuildDir --config $BuildType --parallel

if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }

Write-Host "Build successful!" -ForegroundColor Green

# === Tests =====================================================================
if ($RunTests) {
    Write-Host "Running tests..." -ForegroundColor Cyan
    Push-Location $BuildDir
    ctest --output-on-failure -C $BuildType
    Pop-Location
}

Write-Host @"

=== Build complete ===
Executable: $BuildDir\$BuildType\map_routing.exe

Quick start:
  1. Download OSM data (e.g. city extract from geofabrik.de)
  2. Parse: .\build\Release\map_routing.exe parse city.osm.pbf city.graph
  3. Build: .\build\Release\map_routing.exe build city.graph city.ch
  4. Route: .\build\Release\map_routing.exe route city.graph --ch city.ch --src 0 --dst 1000 --algo ch --out route.geojson
  5. Bench: .\build\Release\map_routing.exe bench city.graph --ch city.ch --pairs 1000 --algo ch
  6. View:  Open route.geojson at https://geojson.io
"@ -ForegroundColor Green
