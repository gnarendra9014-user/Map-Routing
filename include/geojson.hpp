#pragma once
// ============================================================================
//  geojson.hpp — GeoJSON route exporter
// ============================================================================
#include "types.hpp"
#include "graph.hpp"
#include <string>

namespace routing {

// Export a route as a GeoJSON FeatureCollection containing one LineString.
// `route_name` appears in the properties.
// Output is written to `output_path` (or stdout if empty).
void export_geojson(const Graph&      g,
                    const RouteResult& result,
                    const std::string& output_path,
                    const std::string& route_name = "route");

// Returns the GeoJSON as a string (useful for testing)
std::string route_to_geojson(const Graph&       g,
                              const RouteResult& result,
                              const std::string& route_name = "route");

} // namespace routing
