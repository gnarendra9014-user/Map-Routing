# route.ps1 - Easy CLI wrapper for map_routing.exe
# Place in project root. Usage:
#   .\route.ps1 parse
#   .\route.ps1 build
#   .\route.ps1 route --src 0 --dst 1000 --algo ch
#   .\route.ps1 bench --algo ch --pairs 500
#   .\route.ps1 info
#
# Set $EXE and $DATA below if your paths differ.

$EXE   = "$PSScriptRoot\build\Release\map_routing.exe"
$DATA  = "$PSScriptRoot\data"
$PBF   = "$DATA\monaco.osm.pbf"
$GRAPH = "$DATA\monaco.graph"
$CH    = "$DATA\monaco.ch"
$ALT   = "$DATA\monaco.alt"
$ROUTE = "$DATA\route.geojson"

# === Check exe exists ==========================================================
if (-not (Test-Path $EXE)) {
    Write-Host ""
    Write-Host "  map_routing.exe not found at:" -ForegroundColor Red
    Write-Host "  $EXE" -ForegroundColor Red
    Write-Host ""
    Write-Host "  --> Double-click 'bootstrap_and_build.bat' first to build." -ForegroundColor Yellow
    Write-Host ""
    exit 1
}

# === Ensure data directory exists ==============================================
New-Item -ItemType Directory -Force -Path $DATA | Out-Null

# === Parse command =============================================================
$cmd  = if ($args.Count -gt 0) { $args[0] } else { "help" }
$rest = if ($args.Count -gt 1) { $args[1..($args.Count-1)] } else { @() }

switch ($cmd) {

    "download" {
        if (Test-Path $PBF) {
            Write-Host "Monaco PBF already exists: $PBF" -ForegroundColor Green
        } else {
            Write-Host "Downloading Monaco OSM data..." -ForegroundColor Cyan
            Invoke-WebRequest "https://download.geofabrik.de/europe/monaco-latest.osm.pbf" `
                              -OutFile $PBF -UseBasicParsing
            Write-Host "Downloaded: $PBF" -ForegroundColor Green
        }
    }

    "parse" {
        if (-not (Test-Path $PBF)) {
            Write-Host "Monaco PBF not found. Run: .\route.ps1 download" -ForegroundColor Yellow
            exit 1
        }
        Write-Host "Parsing $PBF ..." -ForegroundColor Cyan
        & $EXE parse $PBF $GRAPH
    }

    "build" {
        if (-not (Test-Path $GRAPH)) {
            Write-Host "Graph not found. Run: .\route.ps1 parse" -ForegroundColor Yellow
            exit 1
        }
        Write-Host "Building CH index..." -ForegroundColor Cyan
        & $EXE build $GRAPH $CH
    }

    "build-alt" {
        if (-not (Test-Path $GRAPH)) {
            Write-Host "Graph not found. Run: .\route.ps1 parse" -ForegroundColor Yellow
            exit 1
        }
        Write-Host "Building CH + ALT indexes..." -ForegroundColor Cyan
        & $EXE build $GRAPH $CH --alt $ALT
    }

    "info" {
        if (-not (Test-Path $GRAPH)) {
            Write-Host "Graph not found. Run: .\route.ps1 parse" -ForegroundColor Yellow
            exit 1
        }
        & $EXE info $GRAPH
    }

    "route" {
        if (-not (Test-Path $GRAPH)) {
            Write-Host "Graph not found. Run: .\route.ps1 parse" -ForegroundColor Yellow
            exit 1
        }
        $chArgs = if (Test-Path $CH) { @("--ch", $CH) } else { @() }
        Write-Host "Routing..." -ForegroundColor Cyan
        & $EXE route $GRAPH @chArgs --out $ROUTE @rest
        if ($LASTEXITCODE -eq 0 -and (Test-Path $ROUTE)) {
            Write-Host ""
            Write-Host "Route saved to: $ROUTE" -ForegroundColor Green
            Write-Host "View at: https://geojson.io  (drag and drop the file)" -ForegroundColor Cyan
        }
    }

    "bench" {
        if (-not (Test-Path $GRAPH)) {
            Write-Host "Graph not found. Run: .\route.ps1 parse" -ForegroundColor Yellow
            exit 1
        }
        $chArgs  = if (Test-Path $CH)  { @("--ch",  $CH)  } else { @() }
        $altArgs = if (Test-Path $ALT) { @("--alt", $ALT) } else { @() }
        Write-Host "Benchmarking..." -ForegroundColor Cyan
        & $EXE bench $GRAPH @chArgs @altArgs @rest
    }

    "all" {
        # Full pipeline: download -> parse -> build -> route -> bench
        Write-Host "=== Full pipeline ===" -ForegroundColor Cyan

        if (-not (Test-Path $PBF)) {
            Write-Host "[1/5] Downloading Monaco data..." -ForegroundColor Yellow
            Invoke-WebRequest "https://download.geofabrik.de/europe/monaco-latest.osm.pbf" `
                              -OutFile $PBF -UseBasicParsing
        } else {
            Write-Host "[1/5] Monaco data already downloaded." -ForegroundColor Green
        }

        Write-Host "[2/5] Parsing OSM..." -ForegroundColor Yellow
        & $EXE parse $PBF $GRAPH
        if ($LASTEXITCODE -ne 0) { exit 1 }

        Write-Host "[3/5] Graph info:" -ForegroundColor Yellow
        & $EXE info $GRAPH

        Write-Host "[4/5] Building CH..." -ForegroundColor Yellow
        & $EXE build $GRAPH $CH
        if ($LASTEXITCODE -ne 0) { exit 1 }

        Write-Host "[5/5] Route query (node 0 -> 1000, CH)..." -ForegroundColor Yellow
        & $EXE route $GRAPH --ch $CH --src 0 --dst 1000 --algo ch --out $ROUTE
        if ($LASTEXITCODE -ne 0) { exit 1 }

        Write-Host ""
        Write-Host "=== Benchmark comparison ===" -ForegroundColor Cyan
        foreach ($algo in @("dijkstra", "astar", "bidir", "ch")) {
            Write-Host "--- $algo ---" -ForegroundColor DarkCyan
            $chFlag = if ($algo -eq "ch") { @("--ch", $CH) } else { @() }
            & $EXE bench $GRAPH @chFlag --algo $algo --pairs 200
        }

        Write-Host ""
        Write-Host "Done! Open $ROUTE at https://geojson.io" -ForegroundColor Green
    }

    "open" {
        if (Test-Path $ROUTE) {
            Start-Process "https://geojson.io"
            Write-Host "Drag $ROUTE into the geojson.io browser window." -ForegroundColor Cyan
        } else {
            Write-Host "No route.geojson found. Run: .\route.ps1 route --src 0 --dst 1000 --algo ch" -ForegroundColor Yellow
        }
    }

    default {
        Write-Host ""
        Write-Host "Map Routing Engine - Helper Script" -ForegroundColor Cyan
        Write-Host "Usage: .\route.ps1 <command> [options]" -ForegroundColor White
        Write-Host ""
        Write-Host "Commands:" -ForegroundColor Yellow
        Write-Host "  download          Download Monaco OSM test data"
        Write-Host "  parse             Parse OSM -> graph"
        Write-Host "  build             Build Contraction Hierarchy"
        Write-Host "  build-alt         Build CH + ALT (16-landmark heuristic)"
        Write-Host "  info              Show graph statistics"
        Write-Host "  route [options]   Find a route (pass --src, --dst, --algo)"
        Write-Host "  bench [options]   Benchmark queries (pass --algo, --pairs)"
        Write-Host "  all               Run full pipeline + benchmark"
        Write-Host "  open              Open geojson.io in browser"
        Write-Host ""
        Write-Host "Examples:" -ForegroundColor Yellow
        Write-Host "  .\route.ps1 all"
        Write-Host "  .\route.ps1 route --src 0 --dst 5000 --algo astar"
        Write-Host "  .\route.ps1 bench --algo ch --pairs 1000"
        Write-Host "  .\route.ps1 route --src 100 --dst 8000 --algo dijkstra"
        Write-Host ""
    }
}
