@echo off
:: ============================================================
:: start_server.bat - Launch the Map Router web interface
:: Double-click this file to start the server, then open
:: http://localhost:8080 in your browser.
:: ============================================================
setlocal EnableDelayedExpansion

set "PROJECT=C:\Users\nanin\OneDrive\Desktop\map routing"
set "EXE=!PROJECT!\build\Release\map_routing.exe"
set "GRAPH=!PROJECT!\data\monaco.graph"
set "CH=!PROJECT!\data\monaco.ch"
set "WEB=!PROJECT!\web"

if not exist "!EXE!" (
    echo ERROR: map_routing.exe not found.
    echo Run bootstrap_and_build.bat first to compile the project.
    pause & exit /b 1
)

if not exist "!GRAPH!" (
    echo ERROR: Monaco graph not found at !GRAPH!
    echo Run bootstrap_and_build.bat first to parse the map data.
    pause & exit /b 1
)

echo ============================================================
echo  Map Router - Interactive Routing Visualizer
echo ============================================================
echo.
echo  Starting server...
echo  Open your browser at: http://localhost:8080
echo.
echo  Press Ctrl+C to stop the server.
echo ============================================================
echo.

start "" "http://localhost:8080"

"!EXE!" server "!GRAPH!" --ch "!CH!" --port 8080 --web "!WEB!"

pause
