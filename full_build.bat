@echo off
:: ============================================================
:: full_build.bat — Complete setup and build for map-routing engine
:: Double-click this file, or run it from any terminal.
:: It sets its own PATH — does NOT depend on environment at all.
:: ============================================================
setlocal EnableDelayedExpansion

:: ── Fix PATH to include all system tools ─────────────────────────────────────
set "SYSROOT=C:\Windows\System32"
set "PATH=%SYSROOT%;%SYSROOT%\wbem;%SYSROOT%\WindowsPowerShell\v1.0;C:\Windows;C:\Program Files\Git\cmd;C:\Program Files (x86)\Git\cmd"

:: Verify cmd works
echo [SETUP] Checking system tools...
where powershell.exe >nul 2>&1 || (echo ERROR: powershell.exe not found even in System32 && pause && exit /b 1)
echo   powershell.exe: OK
where cmd.exe >nul 2>&1 || echo   cmd.exe: OK ^(already running^)

:: ── Find Visual Studio via vswhere ───────────────────────────────────────────
echo.
echo [1/5] Locating Visual Studio 2022...
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Is Visual Studio 2022 installed?
    echo Install from: https://visualstudio.microsoft.com/downloads/
    echo Make sure the "Desktop development with C++" workload is selected.
    pause
    exit /b 1
)

for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products Microsoft.VisualStudio.Product.Community Microsoft.VisualStudio.Product.Professional Microsoft.VisualStudio.Product.Enterprise Microsoft.VisualStudio.Product.BuildTools -property installationPath`) do set "VS_PATH=%%i"
)
if not defined VS_PATH (
    echo ERROR: Visual Studio not found via vswhere.
    pause
    exit /b 1
)
echo   VS found at: !VS_PATH!

:: Find VsDevCmd.bat
set "VSDEVCMD=!VS_PATH!\Common7\Tools\VsDevCmd.bat"
if not exist "!VSDEVCMD!" (
    echo ERROR: VsDevCmd.bat not found at !VSDEVCMD!
    pause
    exit /b 1
)
echo   VsDevCmd: !VSDEVCMD!

:: ── Find/verify cmake bundled with VS ────────────────────────────────────────
set "CMAKE_EXE=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "!CMAKE_EXE!" (
    set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
)
if not exist "!CMAKE_EXE!" (
    echo   CMake not found bundled with VS. Downloading...
    powershell.exe -NoProfile -Command ^
        "Invoke-WebRequest -Uri 'https://github.com/Kitware/CMake/releases/download/v3.29.3/cmake-3.29.3-windows-x86_64.zip' -OutFile '%TEMP%\cmake.zip' -UseBasicParsing; Expand-Archive '%TEMP%\cmake.zip' -DestinationPath 'C:\cmake-dl' -Force"
    for /f "delims=" %%f in ('dir /b /s "C:\cmake-dl\cmake.exe" 2^>nul') do set "CMAKE_EXE=%%f"
)
if not exist "!CMAKE_EXE!" (
    echo ERROR: CMake not found. Check VS installation or install CMake from cmake.org
    pause
    exit /b 1
)
echo   CMake: !CMAKE_EXE!

:: ── Setup vcpkg ───────────────────────────────────────────────────────────────
echo.
echo [2/5] Setting up vcpkg...
set "VCPKG_ROOT=C:\vcpkg"
set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"

if not exist "%VCPKG_EXE%" (
    if not exist "%VCPKG_ROOT%\bootstrap-vcpkg.bat" (
        echo   Downloading vcpkg as zip (no git required)...
        powershell.exe -NoProfile -Command ^
            "Invoke-WebRequest -Uri 'https://github.com/microsoft/vcpkg/archive/refs/heads/master.zip' -OutFile '%TEMP%\vcpkg.zip' -UseBasicParsing"
        echo   Extracting vcpkg...
        powershell.exe -NoProfile -Command ^
            "Expand-Archive -Path '%TEMP%\vcpkg.zip' -DestinationPath 'C:\' -Force"
        if exist "C:\vcpkg-master" (
            move /Y "C:\vcpkg-master" "%VCPKG_ROOT%"
        )
    )

    echo   Bootstrapping vcpkg [compiling from source, ~2-3 min]...
    :: Must pass powershell path explicitly since PATH might strip it
    set "POWERSHELL_EXE=C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"
    pushd "%VCPKG_ROOT%"
    call bootstrap-vcpkg.bat -disableMetrics
    popd
)

if not exist "%VCPKG_EXE%" (
    echo ERROR: vcpkg.exe still not found after bootstrap. Check output above.
    pause
    exit /b 1
)
echo   vcpkg: %VCPKG_EXE%

:: ── Install dependencies ──────────────────────────────────────────────────────
echo.
echo [3/5] Installing C++ dependencies via vcpkg...
echo   (libosmium, protozero, zlib, bzip2 - first run may take 5-15 min)
"%VCPKG_EXE%" install --triplet=x64-windows
if errorlevel 1 (
    echo ERROR: vcpkg install failed
    pause
    exit /b 1
)
echo   Dependencies installed.

:: ── Build project ─────────────────────────────────────────────────────────────
echo.
echo [4/5] Building map routing engine...
set "PROJECT_DIR=C:\Users\nanin\OneDrive\Desktop\map routing"
set "BUILD_DIR=%PROJECT_DIR%\build"
set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"

echo Configuring with CMake...
"!CMAKE_EXE!" -S "%PROJECT_DIR%" -B "%BUILD_DIR%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows
if errorlevel 1 (
    echo ERROR: CMake configure failed
    pause
    exit /b 1
)

echo Building...
"!CMAKE_EXE!" --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

:: ── Verify output ─────────────────────────────────────────────────────────────
echo.
echo [5/5] Verifying...
set "EXE=%BUILD_DIR%\Release\map_routing.exe"
if not exist "%EXE%" (
    echo ERROR: Executable not found at %EXE%
    pause
    exit /b 1
)
echo   Executable: %EXE%

:: ── Download Monaco test data ─────────────────────────────────────────────────
echo.
echo ============================================================
echo  BUILD SUCCESSFUL!
echo ============================================================
echo.
set "DATA_DIR=%PROJECT_DIR%\data"
if not exist "%DATA_DIR%" mkdir "%DATA_DIR%"

set "MONACO_PBF=%DATA_DIR%\monaco.osm.pbf"
if not exist "%MONACO_PBF%" (
    echo Downloading Monaco OSM test data [4.5 MB]...
    powershell.exe -NoProfile -Command ^
        "Invoke-WebRequest -Uri 'https://download.geofabrik.de/europe/monaco-latest.osm.pbf' -OutFile '%MONACO_PBF%' -UseBasicParsing"
    echo   Downloaded: %MONACO_PBF%
) else (
    echo Monaco data already exists: %MONACO_PBF%
)

:: --- Run quick smoke test ------------------------------------------------------
echo.
echo Running end-to-end smoke test on Monaco...
echo.

echo [parse] OSM ^-^> graph...
"%EXE%" parse "%MONACO_PBF%" "%DATA_DIR%\monaco.graph"
if errorlevel 1 goto :fail

echo.
echo [info] Graph stats:
"%EXE%" info "%DATA_DIR%\monaco.graph"

echo.
echo [build] Building Contraction Hierarchy...
"%EXE%" build "%DATA_DIR%\monaco.graph" "%DATA_DIR%\monaco.ch"
if errorlevel 1 goto :fail

echo.
echo [route] CH query node 0 ^-^> 1000...
"%EXE%" route "%DATA_DIR%\monaco.graph" --ch "%DATA_DIR%\monaco.ch" --src 0 --dst 1000 --algo ch --out "%DATA_DIR%\route.geojson"
if errorlevel 1 goto :fail

echo.
echo [bench] Benchmarking CH [100 queries]...
"%EXE%" bench "%DATA_DIR%\monaco.graph" --ch "%DATA_DIR%\monaco.ch" --algo ch --pairs 100

echo.
echo ============================================================
echo  ALL DONE!
echo  Route saved to: %DATA_DIR%\route.geojson
echo  Open it at: https://geojson.io
echo ============================================================
pause
exit /b 0

:fail
echo.
echo SMOKE TEST FAILED. See error above.
pause
exit /b 1
