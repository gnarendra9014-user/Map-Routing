@echo off
:: ============================================================
:: bootstrap_and_build.bat
:: Double-click this file in File Explorer to build everything.
:: Handles missing git by downloading Git portable automatically.
:: ============================================================
setlocal EnableDelayedExpansion

:: ── Fix PATH ─────────────────────────────────────────────────────────────────
set "PATH=C:\Windows\System32;C:\Windows;C:\Windows\System32\wbem;C:\Windows\System32\WindowsPowerShell\v1.0"

echo ============================================================
echo  Map Routing Engine — Automated Build
echo ============================================================
echo.

:: ── Find Visual Studio ────────────────────────────────────────────────────────
echo [1/6] Finding Visual Studio 2022...
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" (
    echo ERROR: vswhere.exe not found. Install Visual Studio 2022 with C++ workload.
    pause & exit /b 1
)

:: First try to find VS with C++ compiler tools installed
for /f "usebackq delims=" %%i in (
    `"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`
) do set "VS=%%i"

:: If not found, fall back to any real VS IDE/BuildTools instance (excluding SSMS)
if not defined VS (
    for /f "usebackq delims=" %%i in (
        `"!VSWHERE!" -latest -products Microsoft.VisualStudio.Product.Community Microsoft.VisualStudio.Product.Professional Microsoft.VisualStudio.Product.Enterprise Microsoft.VisualStudio.Product.BuildTools -property installationPath`
    ) do set "VS=%%i"
)

if not defined VS (
    echo ERROR: Visual Studio 2022 not found.
    pause & exit /b 1
)
echo   VS: !VS!

set "VSDEVCMD=!VS!\Common7\Tools\VsDevCmd.bat"
set "CMAKE=!VS!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not exist "!CMAKE!" (
    echo ERROR: CMake not found bundled with VS at: !CMAKE!
    echo   Open Visual Studio Installer and add the "C++ CMake tools" component.
    pause & exit /b 1
)
echo   CMake: !CMAKE!

:: ── Find or install Git ─────────────────────────────────────────────────────
echo.
echo [2/6] Finding Git (required by vcpkg)...

:: Check common git locations
set "GIT_EXE="

:: VS2022 bundled git
set "GIT_VS=!VS!\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe"
if exist "!GIT_VS!" set "GIT_EXE=!GIT_VS!"

:: Standard install locations
if not defined GIT_EXE if exist "C:\Program Files\Git\cmd\git.exe" set "GIT_EXE=C:\Program Files\Git\cmd\git.exe"
if not defined GIT_EXE if exist "C:\Program Files (x86)\Git\cmd\git.exe" set "GIT_EXE=C:\Program Files (x86)\Git\cmd\git.exe"

:: Portable git we may have downloaded previously
if not defined GIT_EXE if exist "C:\git-portable\cmd\git.exe" set "GIT_EXE=C:\git-portable\cmd\git.exe"

if not defined GIT_EXE (
    echo   Git not found anywhere. Downloading Git portable...
    echo   (This is ~50 MB, one-time download)
    echo.

    :: Download MinGit (lightweight portable git, ~30 MB)
    set "MINGIT_ZIP=%TEMP%\MinGit.zip"
    set "MINGIT_DIR=C:\git-portable"

    powershell.exe -NoProfile -Command ^
        "Invoke-WebRequest -Uri 'https://github.com/git-for-windows/git/releases/download/v2.45.2.windows.1/MinGit-2.45.2-64-bit.zip' -OutFile '!MINGIT_ZIP!' -UseBasicParsing"
    if errorlevel 1 (
        echo ERROR: Failed to download Git. Check your internet connection.
        pause & exit /b 1
    )

    echo   Extracting Git portable...
    if not exist "!MINGIT_DIR!" mkdir "!MINGIT_DIR!"
    powershell.exe -NoProfile -Command ^
        "Expand-Archive -Path '!MINGIT_ZIP!' -DestinationPath '!MINGIT_DIR!' -Force"
    if errorlevel 1 (
        echo ERROR: Failed to extract Git.
        pause & exit /b 1
    )

    set "GIT_EXE=!MINGIT_DIR!\cmd\git.exe"
    if not exist "!GIT_EXE!" (
        echo ERROR: git.exe not found after extraction at !GIT_EXE!
        echo   Contents of !MINGIT_DIR!:
        dir "!MINGIT_DIR!" /b
        pause & exit /b 1
    )
)

:: Add git's directory to PATH so vcpkg can find it
for %%F in ("!GIT_EXE!") do set "GIT_DIR=%%~dpF"
set "PATH=!GIT_DIR!;!PATH!"
echo   Git: !GIT_EXE!

:: Verify git works
"!GIT_EXE!" --version
if errorlevel 1 (
    echo ERROR: git --version failed.
    pause & exit /b 1
)

:: ── Bootstrap vcpkg ──────────────────────────────────────────────────────────
echo.
echo [3/6] Setting up vcpkg at C:\vcpkg ...
set "VCPKG_ROOT=C:\vcpkg"
set "VCPKG_EXE=!VCPKG_ROOT!\vcpkg.exe"

if not exist "!VCPKG_ROOT!\bootstrap-vcpkg.bat" (
    echo   Downloading vcpkg...
    powershell.exe -NoProfile -Command ^
        "Invoke-WebRequest -Uri 'https://github.com/microsoft/vcpkg/archive/refs/heads/master.zip' -OutFile '%TEMP%\vcpkg.zip' -UseBasicParsing"
    if errorlevel 1 (
        echo ERROR: Failed to download vcpkg.
        pause & exit /b 1
    )
    echo   Extracting vcpkg...
    powershell.exe -NoProfile -Command ^
        "Expand-Archive -Path '%TEMP%\vcpkg.zip' -DestinationPath 'C:\' -Force"

    :: Rename vcpkg-master to vcpkg
    if exist "C:\vcpkg-master" (
        if exist "!VCPKG_ROOT!" rmdir /s /q "!VCPKG_ROOT!"
        move /Y "C:\vcpkg-master" "!VCPKG_ROOT!" >nul
    )
)

if not exist "!VCPKG_EXE!" (
    echo   Bootstrapping vcpkg [compiling, ~2-3 min]...
    pushd "!VCPKG_ROOT!"
    call bootstrap-vcpkg.bat -disableMetrics
    popd
)

if not exist "!VCPKG_EXE!" (
    echo ERROR: vcpkg.exe not found after bootstrap.
    pause & exit /b 1
)
echo   vcpkg: !VCPKG_EXE!

:: ── Install dependencies ────────────────────────────────────────────────────
echo.
echo [4/6] Installing C++ dependencies via vcpkg...
echo   (libosmium, protozero, zlib, bzip2)
echo   First run may take 5-15 minutes...
echo.

"!VCPKG_EXE!" install --triplet=x64-windows
if errorlevel 1 (
    echo.
    echo ERROR: vcpkg install failed. Common causes:
    echo   - No internet connection
    echo   - Git not working [check above for git errors]
    echo   - Disk full
    echo.
    echo Trying with verbose output...
    "!VCPKG_EXE!" install --triplet=x64-windows --debug
    pause & exit /b 1
)
echo   All dependencies installed.

:: ── Build project ────────────────────────────────────────────────────────────
echo.
echo [5/6] Building map routing engine...

set "PROJECT=C:\Users\nanin\OneDrive\Desktop\map routing"
set "BUILD=!PROJECT!\build"
set "TOOLCHAIN=!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake"

:: Create data dir
if not exist "!PROJECT!\data" mkdir "!PROJECT!\data"

echo Configuring with CMake...
"!CMAKE!" -S "!PROJECT!" -B "!BUILD!" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="!TOOLCHAIN!" -DVCPKG_TARGET_TRIPLET=x64-windows
if errorlevel 1 (
    echo.
    echo ERROR: CMake configure failed.
    pause & exit /b 1
)

echo Building...
"!CMAKE!" --build "!BUILD!" --config Release --parallel
if errorlevel 1 (
    echo.
    echo ERROR: Build failed.
    pause & exit /b 1
)

:: ── Verify ───────────────────────────────────────────────────────────────────
set "EXE=!BUILD!\Release\map_routing.exe"
if not exist "!EXE!" (
    echo ERROR: map_routing.exe not found at: !EXE!
    pause & exit /b 1
)

:: ── Download Monaco test data ────────────────────────────────────────────────
echo.
echo [6/6] Getting Monaco test data...
set "DATA=!PROJECT!\data"
set "PBF=!DATA!\monaco.osm.pbf"

if not exist "!PBF!" (
    echo   Downloading Monaco OSM extract [4.5 MB]...
    powershell.exe -NoProfile -Command ^
        "Invoke-WebRequest -Uri 'https://download.geofabrik.de/europe/monaco-latest.osm.pbf' -OutFile '!PBF!' -UseBasicParsing"
    if not exist "!PBF!" (
        echo   WARNING: Download failed. You can download manually from:
        echo   https://download.geofabrik.de/europe/monaco-latest.osm.pbf
    ) else (
        echo   Downloaded: !PBF!
    )
) else (
    echo   Monaco data already exists.
)

:: ── Run smoke test ───────────────────────────────────────────────────────────
echo.
echo ============================================================
echo  BUILD SUCCESSFUL!
echo  Executable: !EXE!
echo ============================================================
echo.

if exist "!PBF!" (
    echo Running smoke test on Monaco...
    echo.
    echo --- parse ---
    "!EXE!" parse "!PBF!" "!DATA!\monaco.graph"

    echo.
    echo --- info ---
    "!EXE!" info "!DATA!\monaco.graph"

    echo.
    echo --- build CH ---
    "!EXE!" build "!DATA!\monaco.graph" "!DATA!\monaco.ch"

    echo.
    echo --- CH route [node 0 to 1000] ---
    "!EXE!" route "!DATA!\monaco.graph" --ch "!DATA!\monaco.ch" --src 0 --dst 1000 --algo ch --out "!DATA!\route.geojson"

    echo.
    echo --- benchmark [100 random queries per algo] ---
    for %%A in (dijkstra astar bidir ch) do (
        echo.
        echo === %%A ===
        "!EXE!" bench "!DATA!\monaco.graph" --ch "!DATA!\monaco.ch" --algo %%A --pairs 100
    )

    echo.
    echo ============================================================
    echo  ALL DONE!
    echo  Route: !DATA!\route.geojson
    echo  View at: https://geojson.io (drag and drop the file)
    echo ============================================================
)

echo.
pause
exit /b 0
