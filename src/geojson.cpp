// ============================================================================
//  geojson.cpp — GeoJSON route serializer
// ============================================================================
#include "geojson.hpp"
#include "geo.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace routing {

std::string route_to_geojson(const Graph&       g,
                              const RouteResult& result,
                              const std::string& route_name)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(7);

    ss << "{\n"
       << "  \"type\": \"FeatureCollection\",\n"
       << "  \"features\": [\n"
       << "    {\n"
       << "      \"type\": \"Feature\",\n"
       << "      \"geometry\": {\n"
       << "        \"type\": \"LineString\",\n"
       << "        \"coordinates\": [\n";

    // Compute total distance in metres while building coordinate list
    double total_dist_m = 0.0;

    for (size_t i = 0; i < result.path.size(); ++i) {
        NodeId v = result.path[i];
        LatLon c = g.coord(v);
        ss << "          [" << c.lon << ", " << c.lat << "]";
        if (i + 1 < result.path.size()) {
            ss << ",";
            LatLon next = g.coord(result.path[i + 1]);
            total_dist_m += geo::haversine_m(c, next);
        }
        ss << "\n";
    }

    const double duration_s = static_cast<double>(result.cost) / 100.0;

    ss << "        ]\n"
       << "      },\n"
       << "      \"properties\": {\n"
       << "        \"name\": \"" << route_name << "\",\n"
       << "        \"nodes\": " << result.path.size() << ",\n"
       << "        \"distance_m\": " << std::setprecision(1) << total_dist_m << ",\n"
       << "        \"duration_s\": " << std::setprecision(1) << duration_s << ",\n"
       << "        \"duration_min\": " << std::setprecision(2) << (duration_s / 60.0) << "\n"
       << "      }\n"
       << "    }\n"
       << "  ]\n"
       << "}\n";

    return ss.str();
}

void export_geojson(const Graph&       g,
                    const RouteResult& result,
                    const std::string& output_path,
                    const std::string& route_name)
{
    const std::string json = route_to_geojson(g, result, route_name);

    if (output_path.empty()) {
        std::cout << json;
        return;
    }

    std::ofstream f(output_path);
    if (!f) throw std::runtime_error("Cannot write GeoJSON to " + output_path);
    f << json;
    std::cerr << "[GeoJSON] Written to " << output_path << "\n";
}

} // namespace routing
