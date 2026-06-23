# Map Router - Professional Navigation Engine

Map Router is a high-performance interactive map routing visualizer designed to demonstrate advanced pathfinding algorithms on road networks.

## Features

- **Advanced Pathfinding Algorithms**: Compare and visualize Dijkstra, A*, Bidirectional A*, and Contraction Hierarchies (CH) in real-time.
- **Algorithm Performance Dashboard**: Benchmark all algorithms side-by-side, visualizing query times, nodes explored, distance, and duration.
- **Cinematic 3D Tours**: Start a cinematic 3D flythrough of your calculated routes with a beautiful interactive HUD and turn-by-turn navigation instructions.
- **Real-Time Diagnostics**: View real-time search space exploration, query times, and route summary metrics.
- **Interactive Routing Features**:
  - Drag-and-drop start, destination, and waypoints.
  - Optimize waypoint order (TSP).
  - Reverse routes.
  - Choose between driving and walking profiles.
  - Route profiles for Fastest, Scenic, and Green routes.
- **Map Overlays & Traffic Simulation**: Overlay Isochrones, live traffic, and simulate traffic times. Custom avoidance polygons to draw obstacles.
- **Elevation Profiles**: View interactive elevation data along the calculated path.
- **Export Options**: Export routes to GeoJSON and GPX formats or copy a shareable link.

## Technology Stack

- **Frontend**: HTML5, CSS3, JavaScript (Vue 3 for reactivity)
- **Map Rendering**: MapLibre GL JS
- **Routing Engine**: C++ Backend (emulated/connected for algorithms)

## Getting Started

1. Clone this repository.
2. Open `web/index.html` in your web browser.
3. Use the search boxes or click on the map to set start and destination points.
4. Select your preferred routing algorithm and click "Get Route".
5. Explore the 3D Cinematic Tour and Performance Dashboard!
