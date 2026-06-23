// ============================================================================
//  Map Router - Production-Level Vue 3 & MapLibre GL JS Frontend Application
// ============================================================================

const API = ''; // same origin

// CartoDB WebGL Vector Tile Styles
const STYLES = {
    dark: 'https://basemaps.cartocdn.com/gl/dark-matter-gl-style/style.json',
    light: 'https://basemaps.cartocdn.com/gl/positron-gl-style/style.json',
    streets: 'https://basemaps.cartocdn.com/gl/voyager-gl-style/style.json'
};

// Local Landmark Presets (Offline Autocomplete suggestions)
const LOCAL_LANDMARKS = [
    { name: "Casino Monte Carlo", lat: 43.7396, lon: 7.4273, icon: "🎰" },
    { name: "Prince's Palace of Monaco", lat: 43.7308, lon: 7.4200, icon: "🏰" },
    { name: "Port Hercule", lat: 43.7347, lon: 7.4214, icon: "🚢" },
    { name: "Larvotto Beach", lat: 43.7478, lon: 7.4300, icon: "🏖️" },
    { name: "Exotic Garden of Monaco", lat: 43.7385, lon: 7.4140, icon: "🌵" },
    { name: "Oceanographic Museum", lat: 43.7305, lon: 7.4244, icon: "🐠" },
    { name: "Monaco Grand Prix Start/Finish", lat: 43.7371, lon: 7.4206, icon: "🏁" },
    { name: "Monte Carlo Harbor", lat: 43.7402, lon: 7.4265, icon: "⚓" },
    { name: "St. Nicholas Cathedral", lat: 43.7302, lon: 7.4226, icon: "⛪" },
    { name: "Princess Grace Rose Garden", lat: 43.7277, lon: 7.4161, icon: "🌹" }
];

// Preset tour coordinate locations
const SCENIC_TOURS = {
    'gp-circuit': {
        src: { lat: 43.7371, lon: 7.4206, address: "🏁 Monaco Grand Prix Start/Finish" },
        dst: { lat: 43.7396, lon: 7.4273, address: "🎰 Casino Monte Carlo" }
    },
    'palace-casino': {
        src: { lat: 43.7308, lon: 7.4200, address: "🏰 Prince's Palace of Monaco" },
        dst: { lat: 43.7396, lon: 7.4273, address: "🎰 Casino Monte Carlo" }
    },
    'harbor-beach': {
        src: { lat: 43.7347, lon: 7.4214, address: "🚢 Port Hercule" },
        dst: { lat: 43.7478, lon: 7.4300, address: "🏖️ Larvotto Beach" }
    },
    'garden-beach': {
        src: { lat: 43.7385, lon: 7.4140, address: "🌵 Exotic Garden of Monaco" },
        dst: { lat: 43.7478, lon: 7.4300, address: "🏖️ Larvotto Beach" }
    }
};

const { createApp, ref, computed, onMounted, watch } = Vue;

const app = createApp({
    setup() {
        // --- Centralized Reactive State ---
        const connected = ref(false);
        const stats = ref({ nodes: 0, edges: 0 });
        const activeTheme = ref(localStorage.getItem('map-router-theme') || 'dark');
        const activeMapStyle = ref('dark');
        const sidebarCollapsed = ref(false);
        const mobileSheetState = ref('half'); // 'collapsed', 'half', 'expanded'
        const selectedTour = ref('');
        const selectedProfile = ref('car'); // 'car', 'walking'
        const selectedRoutingProfile = ref('fastest'); // 'fastest', 'scenic', 'green'
        const selectedAlgo = ref('dijkstra');
        const loading = ref(false);
        
        // Point configurations: Start A, End B, and Stops List
        const start = ref({ address: '', lat: null, lon: null, suggestions: [], focusedIndex: -1 });
        const end = ref({ address: '', lat: null, lon: null, suggestions: [], focusedIndex: -1 });
        const stops = ref([]); // Array of { id, address, lat, lon, suggestions, focusedIndex }
        const activeClickMode = ref('start'); // 'start', 'end', or stop.id
        
        // Results State
        const routeData = ref(null);
        const comparisonResults = ref({});
        const activeResultsTab = ref('directions');
        const directions = ref([]);
        const elevationData = ref({ polyline: '', area: '', maxElev: 50 });
        const history = ref(JSON.parse(localStorage.getItem('map-router-history') || '[]'));
        
        // Map Overlays Toggles
        const isochroneActive = ref(false);
        const heatmapActive = ref(false);
        const trafficActive = ref(false);
        const trafficTime = ref(720); // 12:00 PM in minutes (0 - 1439)
        const avoidanceActive = ref(false);
        const avoidanceCircles = ref([]); // List of { id, lat, lon, radius }
        
        // Cinematic Tour Playback HUD State
        const tourActive = ref(false);
        const tourPlaying = ref(false);
        const tourProgressPct = ref(0);
        const tourStreet = ref('Cruising Monaco...');
        const tourSpeedKmH = ref(0);
        const tourSpeedMultiplier = ref(2);
        const tourInstruction = ref('');
        const tourInstructionIcon = ref('⬆️');
        
        // Visualizer State (playback console)
        const logs = ref([{ text: 'System Ready.', type: 'system' }]);
        const playbackState = ref('stopped'); // 'stopped', 'playing', 'paused'
        const animSpeed = ref(50);
        const dashboardOpen = ref(false);
        const devPanelOpen = ref(false);

        // Algorithmic Names Map
        const algoNames = {
            dijkstra: 'Dijkstra',
            astar: 'A*',
            bidir: 'Bi-A*',
            ch: 'CH',
            alt: 'ALT',
            hl: 'HL'
        };

        const comparisonOrder = ['dijkstra', 'astar', 'bidir', 'ch', 'alt', 'hl'];
        const algos = ['dijkstra', 'astar', 'bidir', 'ch', 'alt', 'hl'];

        // --- Map references ---
        let map = null;
        let mapMarkers = { start: null, end: null, stops: {} };
        let snapMarker = null;
        let snapSegmentIndex = -1;
        let isDraggingSnapMarker = false;
        
        // Background timers/references
        let tourTimer = null;
        let tourCoords = [];
        let tourIndex = 0;
        let tourVehicleMarker = null;
        let tourTurns = [];
        
        let visualizerFrameId = null;
        let visualizerVisited = [];
        let visualizerIndex = 0;
        let visualizerCallback = null;
        let visualizerColor = '#38bdf8';
        
        // Caches for dynamic overlays
        let cachedIsochrone = null;
        let cachedHeatmap = null;
        let cachedTraffic = null;
        
        // --- Computed Properties ---
        const isRouteReady = computed(() => {
            return start.value.lat !== null && end.value.lat !== null;
        });

        const formattedDistance = computed(() => {
            if (!routeData.value) return '-';
            const km = (routeData.value.distance_m / 1000).toFixed(2);
            return `${km} km`;
        });

        const formattedDuration = computed(() => {
            if (!routeData.value) return '-';
            let durMin = routeData.value.duration_min;
            if (selectedProfile.value === 'walking') {
                durMin = routeData.value.distance_m / 83.3; // 5 km/h walking speed
            }
            if (durMin >= 60) {
                const hrs = Math.floor(durMin / 60);
                const mins = Math.round(durMin % 60);
                return `${hrs}h ${mins}m`;
            }
            return `${durMin.toFixed(1)} min`;
        });

        const formattedTrafficTime = computed(() => {
            const h = Math.floor(trafficTime.value / 60);
            const m = trafficTime.value % 60;
            const ampm = h >= 12 ? 'PM' : 'AM';
            const displayHour = h % 12 === 0 ? 12 : h % 12;
            const displayMin = m < 10 ? '0' + m : m;
            
            let rush = '';
            if (trafficTime.value >= 480 && trafficTime.value <= 570) rush = ' (Morning Rush)';
            else if (trafficTime.value >= 1020 && trafficTime.value <= 1110) rush = ' (Evening Rush)';
            
            return `${displayHour}:${displayMin} ${ampm}${rush}`;
        });

        const showBenchmarkSection = computed(() => {
            return Object.keys(comparisonResults.value).length >= 2;
        });

        const dashboardStats = computed(() => {
            let fastest = null, fastestTime = Infinity;
            let slowest = null, slowestTime = 0;
            let fewestNodes = null, fewestNodesCount = Infinity;
            
            for (const algo of comparisonOrder) {
                const res = comparisonResults.value[algo];
                if (!res) continue;
                if (res.query_ms < fastestTime) {
                    fastestTime = res.query_ms;
                    fastest = algo;
                }
                if (res.query_ms > slowestTime) {
                    slowestTime = res.query_ms;
                    slowest = algo;
                }
                if (res.visited.length < fewestNodesCount) {
                    fewestNodesCount = res.visited.length;
                    fewestNodes = algo;
                }
            }
            return { fastest, slowest, fewestNodes };
        });

        // --- Watchers ---
        watch(activeTheme, (newTheme) => {
            localStorage.setItem('map-router-theme', newTheme);
            document.body.className = newTheme + '-theme';
        }, { immediate: true });

        // Auto trigger route calculations when coordinates change reactively
        watch([
            () => start.value.lat, () => start.value.lon,
            () => end.value.lat, () => end.value.lon,
            () => stops.value.map(s => `${s.lat},${s.lon}`).join('|')
        ], () => {
            syncMapMarkers();
            autoRoute();
        });

        // Resize MapLibre canvas when sidebar collapses/expands
        watch(sidebarCollapsed, () => {
            setTimeout(() => {
                if (map) map.resize();
            }, 300); // matches CSS transitions
        });

        // --- Lifecycle Hooks ---
        onMounted(() => {
            initMap();
            fetchGraphInfo();
            parseUrlParams();
            setupTouchGestures();
            
            // Force map resize to resolve initial collapsed canvas size bug
            setTimeout(() => {
                if (map) map.resize();
            }, 200);
            
            // Global click handler to dismiss geocoder suggestions
            document.addEventListener('click', (e) => {
                if (!e.target.closest('.search-input-wrapper')) {
                    start.value.suggestions = [];
                    end.value.suggestions = [];
                    stops.value.forEach(s => s.suggestions = []);
                }
            });
        });

        // --- Methods ---
        
        // Initialize MapLibre GL
        function initMap() {
            map = new maplibregl.Map({
                container: 'map',
                style: STYLES[activeMapStyle.value],
                center: [7.4246, 43.7384], // Monaco: [lon, lat]
                zoom: 14,
                attributionControl: true
            });

            map.addControl(new maplibregl.NavigationControl({ showCompass: true }), 'bottom-right');

            map.on('style.load', () => {
                map.resize();
                initMapLayers();
                replotRouteAndOverlays();
            });

            map.on('click', onMapClick);
            map.on('mousemove', onMapMouseMove);

            // Setup snapping marker for route dragging
            const snapEl = document.createElement('div');
            snapEl.className = 'route-snap-dot';
            snapMarker = new maplibregl.Marker({
                element: snapEl,
                draggable: true
            });

            snapMarker.on('dragstart', () => {
                isDraggingSnapMarker = true;
                map.getSource('preview-line-source').setData({
                    type: 'FeatureCollection',
                    features: []
                });
                map.setLayoutProperty('preview-line-layer', 'visibility', 'visible');
            });

            snapMarker.on('drag', () => {
                const pos = snapMarker.getLngLat();
                const coords = routeData.value.coordinates;
                const idx = snapSegmentIndex;
                if (idx >= 0 && idx < coords.length - 1) {
                    const p1 = coords[idx];
                    const p2 = coords[idx + 1];
                    // Update preview dashed line representing detour
                    map.getSource('preview-line-source').setData({
                        type: 'Feature',
                        geometry: {
                            type: 'LineString',
                            coordinates: [
                                [p1[1], p1[0]],
                                [pos.lng, pos.lat],
                                [p2[1], p2[0]]
                            ]
                        }
                    });
                }
            });

            snapMarker.on('dragend', handleSnapMarkerDragEnd);
        }

        // Initialize MapLibre GL sources and layers
        function initMapLayers() {
            // 1. Exploration (Search Space) Layer
            map.addSource('exploration-source', {
                type: 'geojson',
                data: { type: 'FeatureCollection', features: [] }
            });
            map.addLayer({
                id: 'exploration-layer',
                type: 'circle',
                source: 'exploration-source',
                paint: {
                    'circle-radius': ['interpolate', ['linear'], ['zoom'], 12, 1.5, 17, 3.5],
                    'circle-color': ['coalesce', ['get', 'color'], '#38bdf8'],
                    'circle-opacity': 0.55
                }
            });

            // 2. Alternative route layers
            map.addSource('alternatives-source', {
                type: 'geojson',
                data: { type: 'FeatureCollection', features: [] }
            });
            map.addLayer({
                id: 'alternatives-layer',
                type: 'line',
                source: 'alternatives-source',
                layout: {
                    'line-join': 'round',
                    'line-cap': 'round'
                },
                paint: {
                    'line-color': '#94a3b8',
                    'line-width': 5.5,
                    'line-opacity': 0.65,
                    'line-dasharray': [2, 1.5]
                }
            });

            // 3. Hover click layer on alternatives to switch route
            map.on('click', 'alternatives-layer', (e) => {
                if (e.features && e.features.length > 0) {
                    const idx = e.features[0].properties.index;
                    selectAlternativeRoute(idx);
                }
            });
            map.on('mouseenter', 'alternatives-layer', () => { map.getCanvas().style.cursor = 'pointer'; });
            map.on('mouseleave', 'alternatives-layer', () => { map.getCanvas().style.cursor = ''; });

            // 4. Primary Route (Glowing Ambient & Active Line)
            map.addSource('route-source', {
                type: 'geojson',
                data: { type: 'FeatureCollection', features: [] }
            });
            
            // Glow layer (wide outline)
            map.addLayer({
                id: 'route-glow-layer',
                type: 'line',
                source: 'route-source',
                layout: { 'line-join': 'round', 'line-cap': 'round' },
                paint: {
                    'line-color': selectedProfile.value === 'walking' ? '#c084fc' : '#38bdf8',
                    'line-width': 10,
                    'line-opacity': 0.22
                }
            });

            // Main path line
            map.addLayer({
                id: 'route-line-layer',
                type: 'line',
                source: 'route-source',
                layout: { 'line-join': 'round', 'line-cap': 'round' },
                paint: {
                    'line-color': selectedProfile.value === 'walking' ? '#c084fc' : '#38bdf8',
                    'line-width': 5,
                    'line-opacity': 0.9
                }
            });

            // 5. Drag preview dashed line
            map.addSource('preview-line-source', {
                type: 'geojson',
                data: { type: 'FeatureCollection', features: [] }
            });
            map.addLayer({
                id: 'preview-line-layer',
                type: 'line',
                source: 'preview-line-source',
                layout: { 'line-join': 'round', 'line-cap': 'round', 'visibility': 'none' },
                paint: {
                    'line-color': '#f59e0b',
                    'line-width': 4,
                    'line-opacity': 0.8,
                    'line-dasharray': [2, 2]
                }
            });

            // 6. Isochrone dots layer
            map.addSource('isochrone-source', {
                type: 'geojson',
                data: { type: 'FeatureCollection', features: [] }
            });
            map.addLayer({
                id: 'isochrone-layer',
                type: 'circle',
                source: 'isochrone-source',
                paint: {
                    'circle-radius': ['interpolate', ['linear'], ['zoom'], 12, 2.5, 17, 5.5],
                    'circle-color': [
                        'match', ['get', 'bucket'],
                        0, '#f43f5e',
                        1, '#f97316',
                        2, '#fbbf24',
                        3, '#38bdf8',
                        '#38bdf8'
                    ],
                    'circle-opacity': [
                        'match', ['get', 'bucket'],
                        0, 0.85,
                        1, 0.70,
                        2, 0.50,
                        3, 0.30,
                        0.40
                    ]
                }
            });

            // 7. Road Network heatmap layer
            map.addSource('heatmap-source', {
                type: 'geojson',
                data: { type: 'FeatureCollection', features: [] }
            });
            map.addLayer({
                id: 'heatmap-layer',
                type: 'circle',
                source: 'heatmap-source',
                paint: {
                    'circle-radius': ['interpolate', ['linear'], ['zoom'], 12, 1.2, 17, 2.8],
                    'circle-color': '#38bdf8',
                    'circle-opacity': 0.28
                }
            });

            // 8. Traffic layers
            map.addSource('traffic-source', {
                type: 'geojson',
                data: { type: 'FeatureCollection', features: [] }
            });
            map.addLayer({
                id: 'traffic-layer',
                type: 'line',
                source: 'traffic-source',
                layout: { 'line-join': 'round', 'line-cap': 'round' },
                paint: {
                    'line-color': ['get', 'color'],
                    'line-width': ['interpolate', ['linear'], ['zoom'], 12, 3, 17, 6],
                    'line-opacity': 0.75
                }
            });

            // 9. Obstacles/Avoidance drawing layer
            map.addSource('avoidance-source', {
                type: 'geojson',
                data: { type: 'FeatureCollection', features: [] }
            });
            map.addLayer({
                id: 'avoidance-fill-layer',
                type: 'fill',
                source: 'avoidance-source',
                paint: {
                    'fill-color': '#ef4444',
                    'fill-opacity': 0.32
                }
            });
            map.addLayer({
                id: 'avoidance-line-layer',
                type: 'line',
                source: 'avoidance-source',
                paint: {
                    'line-color': '#ef4444',
                    'line-width': 1.8,
                    'line-dasharray': [4, 4]
                }
            });

            // Avoidance click listener to remove obstacle zones
            map.on('click', 'avoidance-fill-layer', (e) => {
                if (e.features && e.features.length > 0) {
                    const id = e.features[0].properties.id;
                    removeAvoidanceZone(id);
                }
            });
        }

        // Fetch C++ graph specs
        async function fetchGraphInfo() {
            try {
                const resp = await fetch(`${API}/api/info`);
                const data = await resp.json();
                connected.value = true;
                stats.value.nodes = data.nodes;
                stats.value.edges = data.edges;
                writeLog(`System: Connected to C++ routing engine. Graph loaded successfully (${data.nodes} nodes, ${data.edges} edges).`, 'system');
                
                if (data.bounds) {
                    map.fitBounds([
                        [data.bounds.minLon, data.bounds.minLat],
                        [data.bounds.maxLon, data.bounds.maxLat]
                    ], { padding: 40 });
                }
            } catch (e) {
                connected.value = false;
                writeLog('Error: Could not connect to routing server. Make sure map_routing.exe is running.', 'system');
            }
        }

        // Toggle app theme (dark / light)
        function toggleTheme() {
            activeTheme.value = activeTheme.value === 'dark' ? 'light' : 'dark';
            switchMapStyle(activeTheme.value === 'light' ? 'light' : 'dark');
        }

        // Switch Mapbox style URL
        function switchMapStyle(style) {
            if (!STYLES[style]) return;
            activeMapStyle.value = style;
            map.setStyle(STYLES[style]);
            writeLog(`System: Map style changed to ${style.toUpperCase()}.`, 'system');
        }

        // Sync markers in memory with Vue reactive coordinates
        function syncMapMarkers() {
            if (!map) return;

            // 1. Sync start point A marker
            if (start.value.lat !== null && start.value.lon !== null) {
                if (!mapMarkers.start) {
                    const el = document.createElement('div');
                    el.className = 'custom-marker';
                    el.innerHTML = '<div class="marker-pin src"><span>A</span></div><div class="marker-pulse-ring"></div>';
                    mapMarkers.start = new maplibregl.Marker({ element: el, draggable: true })
                        .setLngLat([start.value.lon, start.value.lat])
                        .addTo(map);
                    mapMarkers.start.on('dragend', () => {
                        const pos = mapMarkers.start.getLngLat();
                        updateCoordinate('start', pos.lat, pos.lng, 'drag');
                    });
                } else {
                    const pos = mapMarkers.start.getLngLat();
                    if (pos.lat !== start.value.lat || pos.lng !== start.value.lon) {
                        mapMarkers.start.setLngLat([start.value.lon, start.value.lat]);
                    }
                }
            } else if (mapMarkers.start) {
                mapMarkers.start.remove();
                mapMarkers.start = null;
            }

            // 2. Sync destination B marker
            if (end.value.lat !== null && end.value.lon !== null) {
                if (!mapMarkers.end) {
                    const el = document.createElement('div');
                    el.className = 'custom-marker';
                    el.innerHTML = '<div class="marker-pin dst"><span>B</span></div><div class="marker-pulse-ring"></div>';
                    mapMarkers.end = new maplibregl.Marker({ element: el, draggable: true })
                        .setLngLat([end.value.lon, end.value.lat])
                        .addTo(map);
                    mapMarkers.end.on('dragend', () => {
                        const pos = mapMarkers.end.getLngLat();
                        updateCoordinate('end', pos.lat, pos.lng, 'drag');
                    });
                } else {
                    const pos = mapMarkers.end.getLngLat();
                    if (pos.lat !== end.value.lat || pos.lng !== end.value.lon) {
                        mapMarkers.end.setLngLat([end.value.lon, end.value.lat]);
                    }
                }
            } else if (mapMarkers.end) {
                mapMarkers.end.remove();
                mapMarkers.end = null;
            }

            // 3. Sync intermediate stops markers
            const currentStopIds = new Set(stops.value.map(s => s.id));

            // Remove deleted stops markers
            for (const id in mapMarkers.stops) {
                if (!currentStopIds.has(Number(id))) {
                    mapMarkers.stops[id].remove();
                    delete mapMarkers.stops[id];
                }
            }

            // Add or update markers
            stops.value.forEach((stop, index) => {
                if (stop.lat !== null && stop.lon !== null) {
                    const label = (index + 1).toString();
                    if (!mapMarkers.stops[stop.id]) {
                        const el = document.createElement('div');
                        el.className = 'custom-marker';
                        el.innerHTML = `<div class="marker-pin waypoint"><span>${label}</span></div><div class="marker-pulse-ring"></div>`;
                        const marker = new maplibregl.Marker({ element: el, draggable: true })
                            .setLngLat([stop.lon, stop.lat])
                            .addTo(map);
                        marker.on('dragend', () => {
                            const pos = marker.getLngLat();
                            updateCoordinate(stop, pos.lat, pos.lng, 'drag');
                        });
                        mapMarkers.stops[stop.id] = marker;
                    } else {
                        const marker = mapMarkers.stops[stop.id];
                        const pos = marker.getLngLat();
                        if (pos.lat !== stop.lat || pos.lng !== stop.lon) {
                            marker.setLngLat([stop.lon, stop.lat]);
                        }
                        // Keep text label matching current index (in case of reordering)
                        const span = marker.getElement().querySelector('.marker-pin span');
                        if (span) span.textContent = label;
                    }
                } else if (mapMarkers.stops[stop.id]) {
                    mapMarkers.stops[stop.id].remove();
                    delete mapMarkers.stops[stop.id];
                }
            });
        }

        // Map Click Event
        function onMapClick(e) {
            const lat = e.lngLat.lat;
            const lon = e.lngLat.lng;

            if (avoidanceActive.value) {
                addAvoidanceZone(lat, lon);
                return;
            }
            if (isochroneActive.value) {
                fetchIsochrone(lat, lon);
                return;
            }

            if (activeClickMode.value === 'start') {
                updateCoordinate('start', lat, lon, 'map click');
            } else if (activeClickMode.value === 'end') {
                updateCoordinate('end', lat, lon, 'map click');
            } else {
                const stop = stops.value.find(s => s.id === activeClickMode.value);
                if (stop) {
                    updateCoordinate(stop, lat, lon, 'map click');
                }
            }
        }

        // Map Hover MouseMove event (for snapping dot)
        function onMapMouseMove(e) {
            if (!routeData.value || tourActive.value || playbackState.value === 'playing' || isDraggingSnapMarker) return;

            const pt = [e.lngLat.lat, e.lngLat.lng]; // [lat, lon]
            const coords = routeData.value.coordinates;
            const closest = getClosestPointOnLine(pt, coords);

            if (closest.distance < 45) { // Hovering within 45m of route line
                snapSegmentIndex = closest.index;
                snapMarker.setLngLat([closest.point[1], closest.point[0]]).addTo(map);
            } else {
                snapMarker.remove();
            }
        }

        // Select coordinates from photon autocomplete or direct map clicks
        async function updateCoordinate(target, lat, lon, source = 'manual', customAddress = null) {
            const textAddress = customAddress || await reverseGeocode(lat, lon);
            
            if (target === 'start') {
                start.value.lat = lat;
                start.value.lon = lon;
                start.value.address = textAddress;
                start.value.suggestions = [];
                writeLog(`System: Set Start Point A to "${textAddress}" (${lat.toFixed(5)}, ${lon.toFixed(5)}).`, 'system');
                if (end.value.lat === null) activeClickMode.value = 'end';
            } else if (target === 'end') {
                end.value.lat = lat;
                end.value.lon = lon;
                end.value.address = textAddress;
                end.value.suggestions = [];
                writeLog(`System: Set Destination B to "${textAddress}" (${lat.toFixed(5)}, ${lon.toFixed(5)}).`, 'system');
            } else {
                // target is a Stop object
                target.lat = lat;
                target.lon = lon;
                target.address = textAddress;
                target.suggestions = [];
                writeLog(`System: Set Waypoint Stop to "${textAddress}" (${lat.toFixed(5)}, ${lon.toFixed(5)}).`, 'system');
            }
        }

        // --- Geocoder Searching / Autocomplete (Offline matches + Komoot Photon Region API) ---
        let searchTimeout = null;
        function onSearchInput(targetName) {
            let pointObj = null;
            if (targetName === 'start') pointObj = start.value;
            else if (targetName === 'end') pointObj = end.value;
            else pointObj = targetName; // is stop object
            
            const query = pointObj.address;
            pointObj.focusedIndex = -1;

            clearTimeout(searchTimeout);
            if (!query || query.length < 2) {
                pointObj.suggestions = [];
                return;
            }

            searchTimeout = setTimeout(async () => {
                // Local presets search
                const localMatches = LOCAL_LANDMARKS.filter(lm => 
                    lm.name.toLowerCase().includes(query.toLowerCase())
                ).map(lm => ({
                    name: lm.name, lat: lm.lat, lon: lm.lon, icon: lm.icon, source: 'Preset'
                }));

                // Photon Region API bounded search
                let photonMatches = [];
                try {
                    const url = `https://photon.komoot.io/api/?q=${encodeURIComponent(query)}&lat=43.7384&lon=7.4246&limit=6`;
                    const resp = await fetch(url);
                    const data = await resp.json();
                    if (data && data.features) {
                        photonMatches = data.features.map(f => {
                            const props = f.properties;
                            const name = props.name || props.street || '';
                            const house = props.housenumber ? ` ${props.housenumber}` : '';
                            const street = props.street && props.name !== props.street ? `, ${props.street}` : '';
                            const city = props.city ? `, ${props.city}` : '';
                            const fullName = `${name}${house}${street}${city}`.replace(/^,\s*/, '');
                            return {
                                name: fullName || 'Street Address',
                                lat: f.geometry.coordinates[1],
                                lon: f.geometry.coordinates[0],
                                source: 'Address',
                                icon: '📍'
                            };
                        }).filter(m => m.lat > 43.70 && m.lat < 43.76 && m.lon > 7.39 && m.lon < 7.46); // Bound to Monaco
                    }
                } catch (e) {
                    console.warn(e);
                }

                // Combine selections
                const results = [...localMatches];
                photonMatches.forEach(pm => {
                    if (!results.some(r => Math.abs(r.lat - pm.lat) < 0.0001 && Math.abs(r.lon - pm.lon) < 0.0001)) {
                        results.push(pm);
                    }
                });

                if (results.length === 0) {
                    pointObj.suggestions = [{ name: 'No matches found in Monaco', disabled: true, icon: '⚠️', source: 'System' }];
                } else {
                    pointObj.suggestions = results;
                }
            }, 250);
        }

        // Suggestions Dropdown List Handlers (Keyboard focus & selection)
        function onSearchFocus(targetName) {
            if (targetName === 'start') activeClickMode.value = 'start';
            else if (targetName === 'end') activeClickMode.value = 'end';
            else activeClickMode.value = targetName.id;
        }

        function navigateSuggestions(pointRef, dir) {
            let point = (pointRef === 'start') ? start.value : (pointRef === 'end') ? end.value : pointRef;
            if (point.suggestions.length === 0) return;
            
            let idx = point.focusedIndex + dir;
            if (idx < 0) idx = point.suggestions.length - 1;
            if (idx >= point.suggestions.length) idx = 0;
            
            point.focusedIndex = idx;
        }

        function selectHighlightedSuggestion(pointRef) {
            let point = (pointRef === 'start') ? start.value : (pointRef === 'end') ? end.value : pointRef;
            if (point.suggestions.length > 0 && point.focusedIndex >= 0) {
                const item = point.suggestions[point.focusedIndex];
                if (!item.disabled) {
                    selectSuggestion(point, item);
                }
            }
        }

        function selectSuggestion(pointRef, item) {
            let point = (pointRef === 'start') ? start.value : (pointRef === 'end') ? end.value : pointRef;
            updateCoordinate(point, item.lat, item.lon, 'search', item.name);
        }

        function closeSuggestions(pointRef) {
            let point = (pointRef === 'start') ? start.value : (pointRef === 'end') ? end.value : pointRef;
            point.suggestions = [];
            point.focusedIndex = -1;
        }

        // Clear search fields & coordinate marker
        function clearPoint(target) {
            if (target === 'start') {
                start.value.lat = null;
                start.value.lon = null;
                start.value.address = '';
                start.value.suggestions = [];
            } else if (target === 'end') {
                end.value.lat = null;
                end.value.lon = null;
                end.value.address = '';
                end.value.suggestions = [];
            } else {
                target.lat = null;
                target.lon = null;
                target.address = '';
                target.suggestions = [];
            }
            autoRoute();
        }

        // Reverse entire routing nodes sequence
        function reverseRoute() {
            if (!isRouteReady.value) return;
            writeLog('System: Reversing route coordinates.', 'system');

            // Swap Start & End values
            const tempStart = { ...start.value };
            start.value.lat = end.value.lat;
            start.value.lon = end.value.lon;
            start.value.address = end.value.address;

            end.value.lat = tempStart.lat;
            end.value.lon = tempStart.lon;
            end.value.address = tempStart.address;

            // Reverse stops in-place
            stops.value.reverse();
            autoRoute();
        }

        // Add dynamically allocated waypoint stops
        function addStopField() {
            const id = Date.now();
            stops.value.push({
                id,
                address: '',
                lat: null,
                lon: null,
                suggestions: [],
                focusedIndex: -1
            });
            activeClickMode.value = id;
            writeLog(`System: Added intermediate waypoint stop #${stops.value.length}.`, 'system');
        }

        // Remove waypoint stop
        function removeStopField(id) {
            stops.value = stops.value.filter(s => s.id !== id);
            writeLog('System: Removed waypoint stop.', 'system');
            autoRoute();
        }

        // Optimize stops sequence (TSP Solver)
        async function optimizeWaypointsOrder() {
            if (stops.value.length === 0) return;

            loading.value = true;
            writeLog('System: Optimizing stops sequence (TSP)...', 'system');

            const coordsList = [
                `${start.value.lat},${start.value.lon}`,
                ...stops.value.map(s => `${s.lat},${s.lon}`),
                `${end.value.lat},${end.value.lon}`
            ];

            const coordsParam = coordsList.join('|');

            try {
                const resp = await fetch(`${API}/api/optimize?coords=${encodeURIComponent(coordsParam)}`);
                const data = await resp.json();

                if (data.optimized && data.optimized.length > 2) {
                    const newStops = [];
                    for (let i = 1; i < data.optimized.length - 1; i++) {
                        const originalStopIdx = data.optimized[i] - 1;
                        newStops.push(stops.value[originalStopIdx]);
                    }
                    stops.value = newStops;

                    writeLog('System: Waypoints reordered successfully.', 'system');
                    autoRoute();
                } else {
                    writeLog('System: Waypoint optimization returned invalid sequence.', 'system');
                }
            } catch (e) {
                console.error(e);
                writeLog('Error: Failed to optimize waypoint sequence.', 'system');
            }
            loading.value = false;
        }

        // Trigger automatic route calculations
        function autoRoute() {
            if (isRouteReady.value) {
                fetchRoute(selectedAlgo.value);
            }
        }

        // --- Fetch Route & Leg Calculations ---
        async function fetchRoute(algo) {
            if (!isRouteReady.value) return;

            loading.value = true;
            resetPlaybackControls();
            writeLog(`Engine: Executing pathfinding using algorithm ${algo.toUpperCase()}...`, 'algo');

            // Legs assembly: A -> Stops -> B
            const pointsList = [
                { lat: start.value.lat, lon: start.value.lon }
            ];
            stops.value.forEach(s => {
                if (s.lat !== null && s.lon !== null) {
                    pointsList.push({ lat: s.lat, lon: s.lon });
                }
            });
            pointsList.push({ lat: end.value.lat, lon: end.value.lon });

            try {
                let success = true;
                let totalQueryTime = 0;
                let totalNearestTime = 0;
                let totalDistance = 0;
                let totalDurationSec = 0;
                let coordinates = [];
                let visited = [];
                let alternatives = [];
                
                // Fetch sequential segments
                for (let i = 0; i < pointsList.length - 1; i++) {
                    const pA = pointsList[i];
                    const pB = pointsList[i+1];
                    
                    let avoidParam = '';
                    if (avoidanceCircles.value.length > 0) {
                        avoidParam = `&avoidance_zones=${encodeURIComponent(JSON.stringify(avoidanceCircles.value))}`;
                    }

                    const url = `${API}/api/route?srcLat=${pA.lat}&srcLon=${pA.lon}` +
                                `&dstLat=${pB.lat}&dstLon=${pB.lon}&algo=${algo}&alternatives=true` +
                                `&time_of_day=${trafficActive.value ? trafficTime.value : -1}` +
                                `&profile=${selectedRoutingProfile.value}` +
                                avoidParam;
                    
                    const resp = await fetch(url);
                    const data = await resp.json();

                    if (!data.found) {
                        success = false;
                        break;
                    }

                    if (data.fallback) {
                        writeLog('Engine: Algorithmic fallback triggered. Precomputed indices do not support dynamic conditions. Fell back to A*.', 'system');
                    }

                    totalQueryTime += data.query_ms;
                    totalNearestTime += data.nearest_ms;
                    totalDistance += data.distance_m;
                    totalDurationSec += data.duration_s;

                    if (coordinates.length > 0) {
                        coordinates = coordinates.concat(data.coordinates.slice(1));
                    } else {
                        coordinates = coordinates.concat(data.coordinates);
                    }
                    visited = visited.concat(data.visited);

                    if (data.alternatives) {
                        alternatives.push(...data.alternatives.map(alt => ({ ...alt, legIndex: i })));
                    }
                }

                loading.value = false;

                if (success) {
                    // Update main point coordinates to matched snapped points
                    if (coordinates.length >= 2) {
                        const snappedStart = coordinates[0];
                        const snappedEnd = coordinates[coordinates.length - 1];

                        if (Math.abs(start.value.lat - snappedStart[0]) > 0.0001) {
                            start.value.lat = snappedStart[0];
                            start.value.lon = snappedStart[1];
                        }
                        if (Math.abs(end.value.lat - snappedEnd[0]) > 0.0001) {
                            end.value.lat = snappedEnd[0];
                            end.value.lon = snappedEnd[1];
                        }
                    }

                    const combinedRoute = {
                        found: true,
                        algo: algo,
                        query_ms: totalQueryTime,
                        nearest_ms: totalNearestTime,
                        distance_m: totalDistance,
                        duration_s: totalDurationSec,
                        duration_min: totalDurationSec / 60,
                        coordinates,
                        visited,
                        alternatives
                    };

                    routeData.value = combinedRoute;
                    comparisonResults.value[algo] = combinedRoute;
                    writeLog(`Engine: Resolved route (${pointsList.length - 1} legs). Checked ${visited.length} nodes. Time: ${totalQueryTime.toFixed(3)} ms.`, 'found');
                    
                    // Render paths on WebGL
                    plotRouteOnMap(combinedRoute);
                    generateTurnByTurnDirections(coordinates);
                    generateElevationSVG(combinedRoute);
                    saveToHistory(combinedRoute);
                } else {
                    routeData.value = null;
                    clearRouteOverlay();
                    alert('A route could not be found connecting all selected points on the network.');
                }
            } catch (e) {
                loading.value = false;
                console.error(e);
            }
        }

        // Plot route lines & search space animations on MapLibre
        function plotRouteOnMap(route) {
            animateSearchSpace(route, () => {
                // Re-enable primary route path line rendering
                const geojsonCoords = route.coordinates.map(c => [c[1], c[0]]);
                map.getSource('route-source').setData({
                    type: 'Feature',
                    geometry: {
                        type: 'LineString',
                        coordinates: geojsonCoords
                    }
                });

                // Set alternatives line sources
                if (route.alternatives && route.alternatives.length > 0) {
                    const features = route.alternatives.map((alt, idx) => ({
                        type: 'Feature',
                        properties: { index: idx },
                        geometry: {
                            type: 'LineString',
                            coordinates: alt.coordinates.map(c => [c[1], c[0]])
                        }
                    }));
                    map.getSource('alternatives-source').setData({
                        type: 'FeatureCollection',
                        features: features
                    });
                } else {
                    map.getSource('alternatives-source').setData({
                        type: 'FeatureCollection',
                        features: []
                    });
                }

                // Fit camera bounds to route coordinates
                if (geojsonCoords.length > 0) {
                    const bounds = new maplibregl.LngLatBounds();
                    geojsonCoords.forEach(coord => bounds.extend(coord));
                    map.fitBounds(bounds, { padding: 50, duration: 1000 });
                }
            });
        }

        // Alternative Route Selection handler
        function selectAlternativeRoute(idx) {
            const main = routeData.value;
            const chosen = main.alternatives[idx];
            writeLog(`System: Switching to alternative route option ${idx+1}.`, 'system');

            // Swap selected path and alternative arrays
            const newAlts = main.alternatives.filter((_, i) => i !== idx);
            newAlts.push({
                coordinates: main.coordinates,
                distance_m: main.distance_m,
                duration_s: main.duration_s,
                duration_min: main.duration_min,
                visited: main.visited,
                query_ms: main.query_ms
            });

            const newRoute = {
                ...chosen,
                algo: main.algo,
                alternatives: newAlts
            };

            routeData.value = newRoute;
            comparisonResults.value[main.algo] = newRoute;
            plotRouteOnMap(newRoute);
            generateTurnByTurnDirections(newRoute.coordinates);
            generateElevationSVG(newRoute);
        }

        // Animate A* / Dijkstra search space exploration
        function animateSearchSpace(route, callback) {
            cancelAnimationFrame(visualizerFrameId);
            map.getSource('exploration-source').setData({ type: 'FeatureCollection', features: [] });
            
            const visited = route.visited || [];
            if (visited.length === 0) {
                callback();
                return;
            }

            const algoColors = {
                dijkstra: '#f97316',
                astar: '#38bdf8',
                bidir: '#c084fc',
                ch: '#10b981'
            };
            visualizerColor = algoColors[route.algo] || '#38bdf8';
            visualizerVisited = visited;
            visualizerIndex = 0;
            visualizerCallback = callback;
            playbackState.value = 'playing';

            writeLog('Visualizer: Exploring search space nodes...', 'system');
            
            stepVisualizer();
        }

        function stepVisualizer() {
            if (playbackState.value !== 'playing') return;

            if (visualizerIndex >= visualizerVisited.length) {
                // Done rendering exploration steps
                writeLog('Visualizer: Exploration finished. Plotting optimized path.', 'found');
                playbackState.value = 'stopped';
                if (visualizerCallback) visualizerCallback();
                return;
            }

            const chunkSize = Math.max(1, Math.ceil((visualizerVisited.length / 100) * (animSpeed.value / 12)));
            const nextIdx = Math.min(visualizerIndex + chunkSize, visualizerVisited.length);

            // Fetch current features block
            const currentPoints = visualizerVisited.slice(0, nextIdx);
            const features = currentPoints.map(pt => ({
                type: 'Feature',
                properties: { color: visualizerColor },
                geometry: {
                    type: 'Point',
                    coordinates: [pt[1], pt[0]] // [lon, lat]
                }
            }));

            map.getSource('exploration-source').setData({
                type: 'FeatureCollection',
                features: features
            });

            // Console visual log feeds
            if (visualizerIndex % 20 === 0 || visualizerIndex === 0) {
                const node = visualizerVisited[visualizerIndex];
                writeLog(`Explore: Settled node #${visualizerIndex} (lat: ${node[0].toFixed(5)}, lon: ${node[1].toFixed(5)})`, 'system');
            }

            visualizerIndex = nextIdx;
            visualizerFrameId = requestAnimationFrame(stepVisualizer);
        }

        // Pause exploration visualizer playback
        function togglePlayPause() {
            if (playbackState.value === 'playing') {
                playbackState.value = 'paused';
                cancelAnimationFrame(visualizerFrameId);
                writeLog(`Visualizer: Paused exploration draw at step #${visualizerIndex}.`, 'system');
            } else if (playbackState.value === 'paused') {
                playbackState.value = 'playing';
                writeLog('Visualizer: Resuming exploration draw.', 'system');
                stepVisualizer();
            }
        }

        // Cancel/Stop exploration visualizer
        function stopExploration() {
            playbackState.value = 'stopped';
            cancelAnimationFrame(visualizerFrameId);
            map.getSource('exploration-source').setData({ type: 'FeatureCollection', features: [] });
            writeLog('Visualizer: Draw cancelled.', 'system');
            if (visualizerCallback) visualizerCallback();
        }

        function resetPlaybackControls() {
            playbackState.value = 'stopped';
            cancelAnimationFrame(visualizerFrameId);
            if (map) {
                map.getSource('exploration-source').setData({ type: 'FeatureCollection', features: [] });
            }
        }

        // Clear all route vectors
        function clearRouteOverlay() {
            routeData.value = null;
            comparisonResults.value = {};
            resetPlaybackControls();
            
            if (map) {
                map.getSource('route-source').setData({ type: 'FeatureCollection', features: [] });
                map.getSource('alternatives-source').setData({ type: 'FeatureCollection', features: [] });
                map.getSource('preview-line-source').setData({ type: 'FeatureCollection', features: [] });
            }
            directions.value = [];
            elevationData.value = { polyline: '', area: '', maxElev: 50 };
        }

        // --- Turn-by-Turn Instruction Parsing (Bearing diff analysis) ---
        function generateTurnByTurnDirections(coords) {
            directions.value = [];
            if (coords.length < 2) return;

            const list = [];
            let currentLegDistance = 0;

            for (let i = 0; i < coords.length - 1; i++) {
                const p1 = coords[i];
                const p2 = coords[i+1];
                const dist = haversineDistance(p1[0], p1[1], p2[0], p2[1]);
                currentLegDistance += dist;

                if (i < coords.length - 2) {
                    const p3 = coords[i+2];
                    const bearing1 = getBearing(p1[0], p1[1], p2[0], p2[1]);
                    const bearing2 = getBearing(p2[0], p2[1], p3[0], p3[1]);
                    
                    let diff = bearing2 - bearing1;
                    if (diff > 180) diff -= 360;
                    if (diff < -180) diff += 360;

                    const absDiff = Math.abs(diff);
                    if (absDiff > 25) {
                        let action = 'straight';
                        let inst = 'Proceed straight';
                        let icon = '⬆️';

                        if (diff >= 25 && diff < 70) { action = 'slight-right'; inst = 'Keep slight right'; icon = '↗️'; }
                        else if (diff >= 70 && diff < 120) { action = 'right'; inst = 'Turn right'; icon = '➡️'; }
                        else if (diff >= 120) { action = 'sharp-right'; inst = 'Turn sharp right'; icon = '↪️'; }
                        else if (diff <= -25 && diff > -70) { action = 'slight-left'; inst = 'Keep slight left'; icon = '↖️'; }
                        else if (diff <= -70 && diff > -120) { action = 'left'; inst = 'Turn left'; icon = '⬅️'; }
                        else if (diff <= -120) { action = 'sharp-left'; inst = 'Turn sharp left'; icon = '↩️'; }

                        list.push({
                            instruction: inst,
                            distance: currentLegDistance,
                            coord: p2,
                            icon
                        });
                        currentLegDistance = 0;
                    }
                }
            }

            list.push({
                instruction: 'Arrive at destination',
                distance: currentLegDistance,
                coord: coords[coords.length - 1],
                icon: '🏁'
            });

            directions.value = list;
        }

        // Pan map camera to turn coordinates
        function panToCoordinate(coord) {
            map.flyTo({ center: [coord[1], coord[0]], zoom: 17, duration: 1200 });
        }

        // --- Elevation Profile Plotting ---
        function generateElevationSVG(route) {
            if (!route.coordinates || route.coordinates.length < 2) {
                elevationData.value = { polyline: '', area: '', maxElev: 50 };
                return;
            }

            let distTotal = 0;
            const points = [];
            let curElev = 10;

            for (let i = 0; i < route.coordinates.length; i++) {
                const coord = route.coordinates[i];
                if (i > 0) {
                    const prev = route.coordinates[i-1];
                    distTotal += haversineDistance(prev[0], prev[1], coord[0], coord[1]);
                }
                // Scenic and Monaco elevation mapping heuristic
                const baseElev = Math.max(0, (coord[0] - 43.725) * 5200 + (coord[1] - 7.41) * 2300);
                curElev = curElev * 0.85 + baseElev * 0.15;
                points.push({ dist: distTotal, elev: curElev });
            }

            const width = 360;
            const height = 120;
            const maxD = distTotal;
            const maxE = Math.max(...points.map(p => p.elev), 50);

            const svgPoints = points.map(p => {
                const x = (p.dist / maxD) * width;
                const y = height - (p.elev / maxE) * (height - 20) - 10;
                return `${x},${y}`;
            });

            const polyline = svgPoints.join(' ');
            const area = `0,${height} ${polyline} ${width},${height}`;

            elevationData.value = { polyline, area, maxElev: maxE };
        }

        // --- Benchmark all algorithms ---
        async function compareAll() {
            if (!isRouteReady.value) return;

            loading.value = true;
            comparisonResults.value = {};
            writeLog('System: Starting multi-algorithm benchmark...', 'system');

            const pointsList = [
                { lat: start.value.lat, lon: start.value.lon }
            ];
            stops.value.forEach(s => {
                if (s.lat !== null && s.lon !== null) {
                    pointsList.push({ lat: s.lat, lon: s.lon });
                }
            });
            pointsList.push({ lat: end.value.lat, lon: end.value.lon });

            const algos = ['dijkstra', 'astar', 'bidir', 'ch'];

            for (const algo of algos) {
                try {
                    let success = true;
                    let totalQueryTime = 0;
                    let totalDistance = 0;
                    let totalDurationSec = 0;
                    let coordinates = [];
                    let visited = [];

                    for (let i = 0; i < pointsList.length - 1; i++) {
                        const pA = pointsList[i];
                        const pB = pointsList[i+1];
                        const url = `${API}/api/route?srcLat=${pA.lat}&srcLon=${pA.lon}` +
                                    `&dstLat=${pB.lat}&dstLon=${pB.lon}&algo=${algo}`;
                        const resp = await fetch(url);
                        const data = await resp.json();

                        if (!data.found) {
                            success = false;
                            break;
                        }

                        totalQueryTime += data.query_ms;
                        totalDistance += data.distance_m;
                        totalDurationSec += data.duration_s;

                        if (coordinates.length > 0) {
                            coordinates = coordinates.concat(data.coordinates.slice(1));
                        } else {
                            coordinates = coordinates.concat(data.coordinates);
                        }
                        visited = visited.concat(data.visited);
                    }

                    if (success) {
                        comparisonResults.value[algo] = {
                            found: true,
                            algo: algo,
                            query_ms: totalQueryTime,
                            distance_m: totalDistance,
                            duration_s: totalDurationSec,
                            duration_min: totalDurationSec / 60,
                            coordinates,
                            visited
                        };
                    }
                } catch (e) {
                    console.error(e);
                }
            }

            loading.value = false;
            writeLog('System: Benchmarking complete. Rendering dashboard comparison panels.', 'system');
            
            showDashboard();
            
            // Plot primary route from benchmarks
            if (comparisonResults.value[selectedAlgo.value]) {
                plotRouteOnMap(comparisonResults.value[selectedAlgo.value]);
            }
        }

        // Chart dimension helper widths
        function getComparisonBarWidth(algo) {
            let max = 0;
            for (const key in comparisonResults.value) {
                max = Math.max(max, comparisonResults.value[key].query_ms);
            }
            if (max === 0) return '0%';
            const val = comparisonResults.value[algo].query_ms;
            return `${Math.max(12, (val / max) * 100)}%`;
        }

        function getDashboardTimeBarWidth(algo) {
            let max = 0;
            for (const key in comparisonResults.value) {
                max = Math.max(max, comparisonResults.value[key].query_ms);
            }
            if (max === 0) return '0%';
            const val = comparisonResults.value[algo].query_ms;
            return `${Math.max(15, (val / max) * 100)}%`;
        }

        function getDashboardNodesBarWidth(algo) {
            let max = 0;
            for (const key in comparisonResults.value) {
                max = Math.max(max, comparisonResults.value[key].visited.length);
            }
            if (max === 0) return '0%';
            const val = comparisonResults.value[algo].visited.length;
            return `${Math.max(15, (val / max) * 100)}%`;
        }

        // Set active profile and trigger auto-reroute
        function setProfile(profile) {
            selectedProfile.value = profile;
            writeLog(`System: Mode profile set to ${profile.toUpperCase()}.`, 'system');
            autoRoute();
        }

        // Set routing profile (fastest, scenic, green)
        function setRoutingProfile(prof) {
            selectedRoutingProfile.value = prof;
            writeLog(`System: Preference profile set to ${prof.toUpperCase()}.`, 'system');
            autoRoute();
        }

        // Set engine algorithm
        function setAlgo(algo) {
            selectedAlgo.value = algo;
            writeLog(`System: Route engine algorithm changed to ${algo.toUpperCase()}.`, 'system');
            if (comparisonResults.value[algo]) {
                plotRouteOnMap(comparisonResults.value[algo]);
                generateTurnByTurnDirections(comparisonResults.value[algo].coordinates);
                generateElevationSVG(comparisonResults.value[algo]);
            } else {
                autoRoute();
            }
        }

        // --- Map Overlays (Isochrone, Heatmap, Traffic, Avoidance) ---

        // 1. Isochrone Generation
        async function fetchIsochrone(lat, lon) {
            loading.value = true;
            writeLog(`System: Querying reachability space for coordinate (${lat.toFixed(5)}, ${lon.toFixed(5)}).`, 'system');
            try {
                const resp = await fetch(`${API}/api/isochrone?lat=${lat}&lon=${lon}&maxTime=15`);
                const data = await resp.json();
                cachedIsochrone = data;
                renderIsochroneOnMap(data);
            } catch (e) {
                console.error(e);
            }
            loading.value = false;
        }

        function renderIsochroneOnMap(data) {
            if (!data) return;
            const features = [];
            data.buckets.forEach((bucket, bIdx) => {
                if (!bucket) return;
                bucket.forEach(coord => {
                    features.push({
                        type: 'Feature',
                        properties: { bucket: bIdx },
                        geometry: {
                            type: 'Point',
                            coordinates: [coord[1], coord[0]]
                        }
                    });
                });
            });

            map.getSource('isochrone-source').setData({
                type: 'FeatureCollection',
                features: features
            });

            // Center marker
            if (mapMarkers.start) mapMarkers.start.remove();
            const el = document.createElement('div');
            el.className = 'custom-marker';
            el.innerHTML = '<div class="marker-pin src" style="background:#10b981;"><span>T</span></div><div class="marker-pulse-ring" style="background:rgba(16,185,129,0.4)"></div>';
            mapMarkers.start = new maplibregl.Marker({ element: el })
                .setLngLat([data.center[1], data.center[0]])
                .addTo(map);

            writeLog('System: Isochrone generated for 15-minute reachability.', 'found');
        }

        function toggleIsochrone() {
            isochroneActive.value = !isochroneActive.value;
            if (isochroneActive.value) {
                if (heatmapActive.value) toggleHeatmap();
                map.getCanvas().style.cursor = 'crosshair';
                writeLog('System: Isochrone mode ON. Click anywhere on the map grid.', 'system');
            } else {
                map.getCanvas().style.cursor = '';
                map.getSource('isochrone-source').setData({ type: 'FeatureCollection', features: [] });
                if (mapMarkers.start) { mapMarkers.start.remove(); mapMarkers.start = null; }
                writeLog('System: Isochrone mode OFF.', 'system');
            }
        }

        // 2. Full C++ Graph Heatmap overlay
        async function fetchNetworkNodes() {
            loading.value = true;
            try {
                const resp = await fetch(`${API}/api/network`);
                const data = await resp.json();
                cachedHeatmap = data;
                renderHeatmapOnMap(data);
            } catch (e) {
                console.error(e);
            }
            loading.value = false;
        }

        function renderHeatmapOnMap(data) {
            if (!data) return;
            const features = data.nodes.map(n => ({
                type: 'Feature',
                geometry: { type: 'Point', coordinates: [n[1], n[0]] }
            }));
            map.getSource('heatmap-source').setData({
                type: 'FeatureCollection',
                features: features
            });
            writeLog(`System: GPU Road network heatmap loaded successfully (${data.nodes.length} nodes).`, 'found');
        }

        function toggleHeatmap() {
            heatmapActive.value = !heatmapActive.value;
            if (heatmapActive.value) {
                if (isochroneActive.value) toggleIsochrone();
                fetchNetworkNodes();
            } else {
                map.getSource('heatmap-source').setData({ type: 'FeatureCollection', features: [] });
                writeLog('System: Road network heatmap disabled.', 'system');
            }
        }

        // 3. Live Traffic simulation
        async function updateTrafficDisplay() {
            if (!trafficActive.value) return;
            try {
                const resp = await fetch(`${API}/api/traffic?time_of_day=${trafficTime.value}`);
                const data = await resp.json();
                cachedTraffic = data;
                renderTrafficOnMap(data);
            } catch (e) {
                console.error(e);
            }
        }

        function renderTrafficOnMap(data) {
            if (!data || !data.congested) return;
            const features = data.congested.map(edge => {
                const factor = edge.factor;
                let color = '#eab308';
                if (factor > 2.0) color = '#b91c1c';
                else if (factor > 1.4) color = '#ef4444';
                return {
                    type: 'Feature',
                    properties: { color: color },
                    geometry: {
                        type: 'LineString',
                        coordinates: edge.coords.map(c => [c[1], c[0]])
                    }
                };
            });
            map.getSource('traffic-source').setData({
                type: 'FeatureCollection',
                features: features
            });
        }

        function toggleTraffic() {
            trafficActive.value = !trafficActive.value;
            if (trafficActive.value) {
                writeLog('System: Traffic simulation active.', 'system');
                updateTrafficDisplay();
            } else {
                map.getSource('traffic-source').setData({ type: 'FeatureCollection', features: [] });
                writeLog('System: Traffic simulation inactive.', 'system');
                autoRoute();
            }
        }

        let trafficSliderTimeout = null;
        function onTrafficSliderInput() {
            // instant slider display feedback
        }

        function onTrafficSliderChange() {
            clearTimeout(trafficSliderTimeout);
            trafficSliderTimeout = setTimeout(() => {
                updateTrafficDisplay();
                autoRoute();
            }, 100);
        }

        // 4. Custom Avoidance zone polygon overlay
        function toggleAvoidanceMode() {
            avoidanceActive.value = !avoidanceActive.value;
            if (avoidanceActive.value) {
                if (isochroneActive.value) toggleIsochrone();
                map.getCanvas().style.cursor = 'crosshair';
                writeLog('System: Obstacle drawing mode enabled. Click on the map to place an avoidance zone.', 'system');
            } else {
                map.getCanvas().style.cursor = '';
                writeLog('System: Obstacle drawing mode disabled.', 'system');
            }
        }

        function addAvoidanceZone(lat, lon) {
            const id = Date.now();
            const radius = 120; // 120m diameter obstacle
            avoidanceCircles.value.push({ id, lat, lon, radius });
            renderAvoidanceZonesOnMap();
            writeLog(`System: Placed 120m obstacle zone at (${lat.toFixed(5)}, ${lon.toFixed(5)}).`, 'system');
            autoRoute();
        }

        function removeAvoidanceZone(id) {
            avoidanceCircles.value = avoidanceCircles.value.filter(c => c.id !== id);
            renderAvoidanceZonesOnMap();
            writeLog('System: Obstacle zone removed.', 'system');
            autoRoute();
        }

        function clearAllAvoidance() {
            avoidanceCircles.value = [];
            renderAvoidanceZonesOnMap();
            writeLog('System: Cleared all obstacle zones.', 'system');
            autoRoute();
        }

        // Convert circle centers & radiuses to MapLibre polygon features
        function renderAvoidanceZonesOnMap() {
            const features = avoidanceCircles.value.map(circle => {
                const steps = 32;
                const coords = [];
                const km = circle.radius / 1000;
                for (let i = 0; i < steps; i++) {
                    const theta = (i / steps) * (2 * Math.PI);
                    const dx = km * Math.cos(theta);
                    const dy = km * Math.sin(theta);
                    
                    const lat = circle.lat + (dy / 111.0);
                    const lon = circle.lon + (dx / (111.0 * Math.cos(circle.lat * Math.PI / 180)));
                    coords.push([lon, lat]);
                }
                coords.push(coords[0]); // close polygon loop
                return {
                    type: 'Feature',
                    properties: { id: circle.id },
                    geometry: {
                        type: 'Polygon',
                        coordinates: [coords]
                    }
                };
            });

            map.getSource('avoidance-source').setData({
                type: 'FeatureCollection',
                features: features
            });
        }

        // Re-render overlays in map when switching styles
        function replotRouteAndOverlays() {
            if (routeData.value) plotRouteOnMap(routeData.value);
            if (isochroneActive.value && cachedIsochrone) renderIsochroneOnMap(cachedIsochrone);
            if (heatmapActive.value && cachedHeatmap) renderHeatmapOnMap(cachedHeatmap);
            if (trafficActive.value && cachedTraffic) renderTrafficOnMap(cachedTraffic);
            renderAvoidanceZonesOnMap();
        }

        // --- Cinematic 3D Flythrough Tour ---
        function startCinematicTour() {
            if (!routeData.value || routeData.value.coordinates.length < 2) {
                alert('Please calculate a route first before starting the cinematic tour.');
                return;
            }

            if (playbackState.value === 'playing') {
                stopExploration();
            }

            tourActive.value = true;
            tourPlaying.value = true;
            tourIndex = 0;
            
            // Sub-interpolate coordinates for fluid camera panning movement
            tourCoords = interpolateCoordinates(routeData.value.coordinates, 9);
            
            // Map each direction coordinate to the closest interpolated coordinate index
            tourTurns = directions.value.map(d => {
                const idx = tourCoords.findIndex(c => Math.abs(c[0] - d.coord[0]) < 1e-6 && Math.abs(c[1] - d.coord[1]) < 1e-6);
                return { ...d, tourIdx: idx };
            }).filter(t => t.tourIdx !== -1);
            
            document.getElementById('tour-hud').style.display = 'block';
            tourStreet.value = 'Stitching path...';
            tourProgressPct.value = 0;
            tourSpeedKmH.value = 0;

            if (tourVehicleMarker) tourVehicleMarker.remove();

            const cStart = tourCoords[0];
            const el = document.createElement('div');
            el.className = 'tour-vehicle-marker';
            el.innerHTML = `
                <svg class="vehicle-marker-svg" width="34" height="34" viewBox="0 0 30 30" fill="none" style="transform-origin: center;">
                    <circle cx="15" cy="15" r="12" fill="#070913" stroke="#38bdf8" stroke-width="2.5"/>
                    <polygon points="15,6 21,20 15,16 9,20" fill="#38bdf8"/>
                </svg>
            `;

            tourVehicleMarker = new maplibregl.Marker({ element: el })
                .setLngLat([cStart[1], cStart[0]])
                .addTo(map);

            map.jumpTo({ center: [cStart[1], cStart[0]], zoom: 17.5, pitch: 65 });
            writeLog('Tour: Starting cinematic 3D flythrough navigation.', 'system');

            runTourStep();
        }

        function runTourStep() {
            if (!tourActive.value || !tourPlaying.value) return;

            if (tourIndex >= tourCoords.length) {
                stopTour();
                return;
            }

            const curr = tourCoords[tourIndex];
            const next = tourIndex < tourCoords.length - 1 ? tourCoords[tourIndex + 1] : null;

            let bearing = 0;
            if (next) {
                bearing = calculateBearing(curr[0], curr[1], next[0], next[1]);
            } else if (tourIndex > 0) {
                const prev = tourCoords[tourIndex - 1];
                bearing = calculateBearing(prev[0], prev[1], curr[0], curr[1]);
            }

            tourVehicleMarker.setLngLat([curr[1], curr[0]]);
            
            const svg = tourVehicleMarker.getElement().querySelector('svg');
            if (svg) svg.style.transform = `rotate(${bearing}deg)`;

            // Map camera ease animation
            map.easeTo({
                center: [curr[1], curr[0]],
                bearing: bearing,
                pitch: 65,
                zoom: map.getZoom(),
                duration: 120,
                easing: (t) => t // linear
            });

            tourSpeedKmH.value = Math.floor(35 + Math.sin(tourIndex / 6) * 12 + Math.random() * 5);
            
            // Geographic location labeling HUD
            let street = 'Avenue Princesse Grace';
            if (tourIndex < tourCoords.length * 0.20) street = 'Departing origin...';
            else if (tourIndex > tourCoords.length * 0.85) street = 'Approaching destination...';
            else {
                if (curr[0] > 43.739 && curr[1] > 7.426) street = 'Monte Carlo / Casino Gardens';
                else if (curr[0] < 43.732 && curr[1] < 7.422) street = "The Rock / Prince's Palace";
                else if (curr[0] > 43.733 && curr[0] < 43.737 && curr[1] > 7.419 && curr[1] < 7.423) street = 'Port Hercule Harbor Road';
                else street = 'Grand Prix Track Corridor';
            }
            tourStreet.value = street;
            tourProgressPct.value = (tourIndex / (tourCoords.length - 1)) * 100;

            // Dynamically calculate remaining distance to next turn
            const nextTurn = tourTurns.find(t => t.tourIdx >= tourIndex);
            if (nextTurn) {
                let distToTurn = 0;
                for (let i = tourIndex; i < nextTurn.tourIdx; i++) {
                    distToTurn += haversineDistance(
                        tourCoords[i][0], tourCoords[i][1],
                        tourCoords[i+1][0], tourCoords[i+1][1]
                    );
                }
                tourInstructionIcon.value = nextTurn.icon;
                if (distToTurn < 15) {
                    tourInstruction.value = nextTurn.instruction;
                } else {
                    tourInstruction.value = `${nextTurn.instruction} in ${formatDistance(distToTurn)}`;
                }
            } else {
                tourInstruction.value = 'Arrive at destination';
                tourInstructionIcon.value = '🏁';
            }

            tourIndex++;
            const delay = Math.max(20, 160 / tourSpeedMultiplier.value);
            tourTimer = setTimeout(runTourStep, delay);
        }

        function toggleTourPlayPause() {
            tourPlaying.value = !tourPlaying.value;
            if (tourPlaying.value) {
                runTourStep();
            } else {
                clearTimeout(tourTimer);
            }
        }

        function stopTour() {
            tourActive.value = false;
            tourPlaying.value = false;
            clearTimeout(tourTimer);
            tourInstruction.value = '';
            
            if (tourVehicleMarker) {
                tourVehicleMarker.remove();
                tourVehicleMarker = null;
            }
            map.easeTo({ pitch: 0, bearing: 0, duration: 800 });
            writeLog('Tour: Flythrough tour stopped.', 'system');
        }

        // Sub-interpolate path coordinates
        function interpolateCoordinates(coords, steps = 8) {
            const list = [];
            for (let i = 0; i < coords.length - 1; i++) {
                const p1 = coords[i];
                const p2 = coords[i+1];
                list.push(p1);
                for (let s = 1; s < steps; s++) {
                    const t = s / steps;
                    const lat = p1[0] + (p2[0] - p1[0]) * t;
                    const lon = p1[1] + (p2[1] - p1[1]) * t;
                    list.push([lat, lon]);
                }
            }
            list.push(coords[coords.length - 1]);
            return list;
        }

        // --- Route history management ---
        function saveToHistory(route) {
            const srcAddr = start.value.address;
            const dstAddr = end.value.address;
            if (!srcAddr || !dstAddr) return;

            const item = {
                id: Date.now(),
                srcName: srcAddr,
                dstName: dstAddr,
                srcLat: start.value.lat,
                srcLon: start.value.lon,
                dstLat: end.value.lat,
                dstLon: end.value.lon,
                distance_km: (route.distance_m / 1000).toFixed(2),
                duration: route.duration_min >= 60 
                    ? `${Math.floor(route.duration_min/60)}h ${Math.round(route.duration_min%60)}m` 
                    : `${route.duration_min.toFixed(1)}m`
            };

            history.value.unshift(item);
            if (history.value.length > 10) history.value = history.value.slice(0, 10);
            localStorage.setItem('map-router-history', JSON.stringify(history.value));
        }

        function loadHistoryItem(item) {
            writeLog('System: Recalling route from history.', 'system');
            clearAll();
            
            start.value.lat = item.srcLat;
            start.value.lon = item.srcLon;
            start.value.address = item.srcName;

            end.value.lat = item.dstLat;
            end.value.lon = item.dstLon;
            end.value.address = item.dstName;
        }

        function removeHistoryItem(id) {
            history.value = history.value.filter(x => x.id !== id);
            localStorage.setItem('map-router-history', JSON.stringify(history.value));
        }

        // Clear everything
        function clearAll() {
            start.value = { address: '', lat: null, lon: null, suggestions: [], focusedIndex: -1 };
            end.value = { address: '', lat: null, lon: null, suggestions: [], focusedIndex: -1 };
            stops.value = [];
            selectedTour.value = '';
            
            clearRouteOverlay();
            clearAllAvoidance();
            if (tourActive.value) stopTour();
            resetPlaybackControls();

            writeLog('System: Map values and parameters reset.', 'system');
        }

        // Load Preset Scenic Tour
        function loadScenicTour() {
            const tour = SCENIC_TOURS[selectedTour.value];
            if (!tour) return;
            
            clearAll();
            
            start.value.lat = tour.src.lat;
            start.value.lon = tour.src.lon;
            start.value.address = tour.src.address;

            end.value.lat = tour.dst.lat;
            end.value.lon = tour.dst.lon;
            end.value.address = tour.dst.address;
        }

        // Parse query params for shared links
        function parseUrlParams() {
            const params = new URLSearchParams(window.location.search);
            const src = params.get('src');
            const dst = params.get('dst');
            const algo = params.get('algo');

            if (algo) selectedAlgo.value = algo;
            if (src && dst) {
                const s = src.split(',');
                const d = dst.split(',');
                if (s.length === 2 && d.length === 2) {
                    start.value.lat = parseFloat(s[0]);
                    start.value.lon = parseFloat(s[1]);
                    start.value.address = `Coordinate (${start.value.lat.toFixed(5)}, ${start.value.lon.toFixed(5)})`;

                    end.value.lat = parseFloat(d[0]);
                    end.value.lon = parseFloat(d[1]);
                    end.value.address = `Coordinate (${end.value.lat.toFixed(5)}, ${end.value.lon.toFixed(5)})`;
                }
            }
        }

        // Copy share link
        function copyShareLink() {
            if (!isRouteReady.value) return;
            const params = new URLSearchParams();
            params.set('src', `${start.value.lat},${start.value.lon}`);
            params.set('dst', `${end.value.lat},${end.value.lon}`);
            params.set('algo', selectedAlgo.value);
            
            const url = `${window.location.origin}${window.location.pathname}?${params.toString()}`;
            navigator.clipboard.writeText(url).then(() => {
                showToastNotification('Link copied to clipboard!');
            });
        }

        // Export route as GeoJSON file
        function exportToGeoJSON() {
            if (!routeData.value) return;
            const geojson = {
                type: "FeatureCollection",
                features: [{
                    type: "Feature",
                    properties: {
                        name: "Route Plan",
                        algorithm: routeData.value.algo,
                        distance_km: (routeData.value.distance_m / 1000).toFixed(3),
                        duration_min: routeData.value.duration_min.toFixed(2),
                        profile: selectedProfile.value
                    },
                    geometry: {
                        type: "LineString",
                        coordinates: routeData.value.coordinates.map(c => [c[1], c[0]])
                    }
                }]
            };
            downloadBlob(JSON.stringify(geojson, null, 2), 'route.geojson', 'application/json');
            writeLog('System: Exported route as GeoJSON.', 'system');
        }

        // Export route as GPX file
        function exportToGPX() {
            if (!routeData.value) return;
            let gpx = '<?xml version="1.0" encoding="UTF-8"?>\n';
            gpx += '<gpx version="1.1" creator="MapRouter" xmlns="http://www.topografix.com/GPX/1/1">\n';
            gpx += '  <metadata>\n    <name>MapRouter Route Plan</name>\n  </metadata>\n  <trk>\n    <name>Shortest Path</name>\n    <trkseg>\n';
            routeData.value.coordinates.forEach(c => {
                gpx += `      <trkpt lat="${c[0]}" lon="${c[1]}"></trkpt>\n`;
            });
            gpx += '    </trkseg>\n  </trk>\n</gpx>\n';
            downloadBlob(gpx, 'route.gpx', 'application/gpx+xml');
            writeLog('System: Exported route as GPX file.', 'system');
        }

        function downloadBlob(content, filename, mime) {
            const blob = new Blob([content], { type: mime });
            const a = document.createElement('a');
            a.href = URL.createObjectURL(blob);
            a.download = filename;
            a.click();
            URL.revokeObjectURL(a.href);
        }

        // --- Dragging / Detour Snapping Logic ---
        async function handleSnapMarkerDragEnd() {
            isDraggingSnapMarker = false;
            map.setLayoutProperty('preview-line-layer', 'visibility', 'none');
            
            const pos = snapMarker.getLngLat();
            snapMarker.remove();

            loading.value = true;
            writeLog('System: Searching nearest road network node for detour...', 'system');
            
            try {
                const resp = await fetch(`${API}/api/nearest?lat=${pos.lat}&lon=${pos.lng}`);
                const data = await resp.json();

                if (data.node !== undefined) {
                    // Identify correct insertion index using the Cheapest Insertion Heuristic
                    const insertIdx = findCheapestInsertionIndex(data.lat, data.lon);
                    
                    const address = `Detour (${data.lat.toFixed(5)}, ${data.lon.toFixed(5)})`;
                    const newStop = {
                        id: Date.now(),
                        address: address,
                        lat: data.lat,
                        lon: data.lon,
                        suggestions: [],
                        focusedIndex: -1
                    };

                    stops.value.splice(insertIdx, 0, newStop);
                    writeLog(`System: Inserted detour waypoint stop at index ${insertIdx + 1}.`, 'system');
                }
            } catch (e) {
                console.error(e);
            }
            loading.value = false;
        }

        // Cheapest Insertion Heuristic: finds the insertion index in stops that minimizes detour distance
        function findCheapestInsertionIndex(lat, lon) {
            let bestIdx = 0;
            let minIncrease = Infinity;

            const nodes = [
                { lat: start.value.lat, lon: start.value.lon },
                ...stops.value.map(s => ({ lat: s.lat, lon: s.lon })),
                { lat: end.value.lat, lon: end.value.lon }
            ];

            for (let i = 0; i < nodes.length - 1; i++) {
                const p1 = nodes[i];
                const p2 = nodes[i+1];
                
                const d1 = haversineDistance(p1.lat, p1.lon, lat, lon);
                const d2 = haversineDistance(lat, lon, p2.lat, p2.lon);
                const dDirect = haversineDistance(p1.lat, p1.lon, p2.lat, p2.lon);
                
                const increase = d1 + d2 - dDirect;
                if (increase < minIncrease) {
                    minIncrease = increase;
                    bestIdx = i;
                }
            }
            return bestIdx;
        }

        // --- Touch Gestures / Responsive Sliding Bottom Sheet Drawer ---
        let touchStartY = 0;
        let sheetStartY = 0;
        
        function setupTouchGestures() {
            // Support click on handle to cycle sheet states on mobile
            const handle = document.querySelector('.mobile-drag-handle');
            if (handle) {
                handle.addEventListener('click', (e) => {
                    // Only process click if there was no drag movement
                    if (e.defaultPrevented) return;
                    cycleMobileSheetState();
                });
            }
        }

        function cycleMobileSheetState() {
            if (mobileSheetState.value === 'collapsed') mobileSheetState.value = 'half';
            else if (mobileSheetState.value === 'half') mobileSheetState.value = 'expanded';
            else mobileSheetState.value = 'collapsed';
        }

        function handleTouchStart(e) {
            touchStartY = e.touches[0].clientY;
            initDragGesture();
        }

        function handleMouseDown(e) {
            touchStartY = e.clientY;
            initDragGesture(true);
        }

        function initDragGesture(isMouse = false) {
            const sidebar = document.getElementById('sidebar');
            const rect = sidebar.getBoundingClientRect();
            sheetStartY = rect.top;

            function onDragMove(ev) {
                const clientY = isMouse ? ev.clientY : ev.touches[0].clientY;
                const deltaY = clientY - touchStartY;
                const targetTop = sheetStartY + deltaY;
                const viewportH = window.innerHeight;
                
                // Allow free sliding dragging position limits
                const dragPct = (targetTop / viewportH) * 100;
                
                // Apply temporary styles
                sidebar.style.transition = 'none';
                sidebar.style.transform = `translateY(${Math.max(15, Math.min(92, dragPct))}vh)`;
            }

            function onDragEnd(ev) {
                sidebar.style.transition = '';
                sidebar.style.transform = '';
                
                const clientY = isMouse ? ev.clientY : (ev.changedTouches ? ev.changedTouches[0].clientY : touchStartY);
                const deltaY = clientY - touchStartY;
                const viewportH = window.innerHeight;
                const finalTopPct = ((sheetStartY + deltaY) / viewportH) * 100;

                // Snap to closest sheet state based on height thresholds
                if (finalTopPct > 72) {
                    mobileSheetState.value = 'collapsed';
                } else if (finalTopPct < 32) {
                    mobileSheetState.value = 'expanded';
                } else {
                    mobileSheetState.value = 'half';
                }

                if (isMouse) {
                    document.removeEventListener('mousemove', onDragMove);
                    document.removeEventListener('mouseup', onDragEnd);
                } else {
                    document.removeEventListener('touchmove', onDragMove);
                    document.removeEventListener('touchend', onDragEnd);
                }
            }

            if (isMouse) {
                document.addEventListener('mousemove', onDragMove);
                document.addEventListener('mouseup', onDragEnd);
            } else {
                document.addEventListener('touchmove', onDragMove);
                document.addEventListener('touchend', onDragEnd);
            }
        }

        // --- Utility Helper Functions ---
        function writeLog(text, type = 'system') {
            logs.value.push({ text, type });
            const box = document.getElementById('console-logs');
            if (box) {
                requestAnimationFrame(() => {
                    box.scrollTop = box.scrollHeight;
                });
            }
        }

        function showDashboard() {
            dashboardOpen.value = true;
        }

        function closeDashboard() {
            dashboardOpen.value = false;
        }

        function showToastNotification(msg) {
            let toast = document.getElementById('share-toast');
            if (!toast) {
                toast = document.createElement('div');
                toast.id = 'share-toast';
                toast.className = 'share-toast';
                document.body.appendChild(toast);
            }
            toast.textContent = msg;
            toast.classList.add('show');
            setTimeout(() => { toast.classList.remove('show'); }, 2800);
        }

        return {
            connected,
            stats,
            activeTheme,
            activeMapStyle,
            sidebarCollapsed,
            mobileSheetState,
            selectedTour,
            selectedProfile,
            selectedRoutingProfile,
            selectedAlgo,
            loading,
            start,
            end,
            stops,
            activeClickMode,
            routeData,
            comparisonResults,
            activeResultsTab,
            directions,
            elevationData,
            history,
            isochroneActive,
            heatmapActive,
            trafficActive,
            trafficTime,
            avoidanceActive,
            avoidanceCircles,
            tourActive,
            tourPlaying,
            tourProgressPct,
            tourStreet,
            tourSpeedKmH,
            tourSpeedMultiplier,
            tourInstruction,
            tourInstructionIcon,
            logs,
            playbackState,
            animSpeed,
            dashboardOpen,
            algoNames,
            comparisonOrder,
            algos,
            isRouteReady,
            formattedDistance,
            formattedDuration,
            formattedTrafficTime,
            showBenchmarkSection,
            dashboardStats,
            toggleTheme,
            switchMapStyle,
            onSearchInput,
            onSearchFocus,
            navigateSuggestions,
            selectHighlightedSuggestion,
            selectSuggestion,
            closeSuggestions,
            clearPoint,
            reverseRoute,
            addStopField,
            removeStopField,
            fetchRoute,
            compareAll,
            getComparisonBarWidth,
            getDashboardTimeBarWidth,
            getDashboardNodesBarWidth,
            setProfile,
            setRoutingProfile,
            setAlgo,
            toggleIsochrone,
            toggleHeatmap,
            toggleTraffic,
            onTrafficSliderInput,
            onTrafficSliderChange,
            toggleAvoidanceMode,
            clearAllAvoidance,
            startCinematicTour,
            toggleTourPlayPause,
            stopTour,
            loadHistoryItem,
            removeHistoryItem,
            clearAll,
            loadScenicTour,
            copyShareLink,
            exportToGeoJSON,
            exportToGPX,
            togglePlayPause,
            stopExploration,
            closeDashboard,
            handleTouchStart,
            handleMouseDown,
            panToCoordinate,
            formatDistance,
            formatQueryTime,
            devPanelOpen,
            optimizeWaypointsOrder
        };
    }
});

// --- Non-Reactive Math Helper Algorithms ---

// Distance from target pt to line segment [a, b] projection
function getClosestPointOnLine(pt, coords) {
    let minD = Infinity;
    let closestPt = null;
    let closestIdx = 0;
    
    for (let i = 0; i < coords.length - 1; i++) {
        const p1 = coords[i];
        const p2 = coords[i+1];
        const proj = projectPointOnSegment(pt, p1, p2);
        const d = haversineDistance(pt[0], pt[1], proj[0], proj[1]);
        if (d < minD) {
            minD = d;
            closestPt = proj;
            closestIdx = i;
        }
    }
    return { point: closestPt, distance: minD, index: closestIdx };
}

function projectPointOnSegment(p, a, b) {
    const atob = [b[0] - a[0], b[1] - a[1]];
    const atop = [p[0] - a[0], p[1] - a[1]];
    const len = atob[0] * atob[0] + atob[1] * atob[1];
    let t = 0;
    if (len > 0) {
        t = (atop[0] * atob[0] + atop[1] * atob[1]) / len;
        t = Math.max(0, Math.min(1, t));
    }
    return [a[0] + t * atob[0], a[1] + t * atob[1]];
}

// Distance helper (Haversine)
function haversineDistance(lat1, lon1, lat2, lon2) {
    const R = 6371000; // meters
    const phi1 = lat1 * Math.PI / 180;
    const phi2 = lat2 * Math.PI / 180;
    const deltaPhi = (lat2 - lat1) * Math.PI / 180;
    const deltaLambda = (lon2 - lon1) * Math.PI / 180;
    const a = Math.sin(deltaPhi / 2) * Math.sin(deltaPhi / 2) +
              Math.cos(phi1) * Math.cos(phi2) *
              Math.sin(deltaLambda / 2) * Math.sin(deltaLambda / 2);
    const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
    return R * c;
}

// Coordinate bearing calculator
function getBearing(lat1, lon1, lat2, lon2) {
    const dLon = (lon2 - lon1) * Math.PI / 180;
    const lat1Rad = lat1 * Math.PI / 180;
    const lat2Rad = lat2 * Math.PI / 180;
    const y = Math.sin(dLon) * Math.cos(lat2Rad);
    const x = Math.cos(lat1Rad) * Math.sin(lat2Rad) - Math.sin(lat1Rad) * Math.cos(lat2Rad) * Math.cos(dLon);
    let brng = Math.atan2(y, x) * 180 / Math.PI;
    return (brng + 360) % 360;
}

function calculateBearing(lat1, lon1, lat2, lon2) {
    return getBearing(lat1, lon1, lat2, lon2);
}

// Format meters to km or m
function formatDistance(dist) {
    if (dist >= 1000) {
        return `${(dist / 1000).toFixed(2)} km`;
    }
    return `${Math.round(dist)} m`;
}

// Format query time string
function formatQueryTime(ms) {
    return ms < 1 ? ms.toFixed(3) + ' ms' : ms.toFixed(2) + ' ms';
}

// Reverse Geocoding (photon reverse API snaps)
async function reverseGeocode(lat, lon) {
    // 1. check nearest local presets Snap threshold
    let snap = null;
    let minD = 50; // meters threshold
    for (const lm of LOCAL_LANDMARKS) {
        const d = haversineDistance(lat, lon, lm.lat, lm.lon);
        if (d < minD) {
            minD = d;
            snap = lm;
        }
    }
    if (snap) return `${snap.icon} ${snap.name}`;

    // 2. Photon Reverse Geocoding
    try {
        const resp = await fetch(`https://photon.komoot.io/reverse?lon=${lon}&lat=${lat}`);
        const data = await resp.json();
        if (data && data.features && data.features.length > 0) {
            const props = data.features[0].properties;
            const name = props.name || props.street || '';
            const house = props.housenumber ? ` ${props.housenumber}` : '';
            const street = props.street && props.name !== props.street ? `, ${props.street}` : '';
            const fullName = `${name}${house}${street}`.replace(/^,\s*/, '');
            if (fullName) return fullName;
        }
    } catch (e) {
        console.warn(e);
    }
    return `Coordinate (${lat.toFixed(5)}, ${lon.toFixed(5)})`;
}

// Mount Vue Application
app.mount('#app');
