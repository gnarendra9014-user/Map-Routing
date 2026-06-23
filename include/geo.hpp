#pragma once
// ============================================================================
//  geo.hpp — Haversine distance and speed/time helpers
// ============================================================================
#include "types.hpp"
// _USE_MATH_DEFINES is needed on MSVC to expose M_PI — but we define our
// own constexpr pi to be fully portable without that macro.
#include <cmath>
#include <cstdlib>   // strtod
#include <string_view>

namespace routing::geo {

// ── Haversine great-circle distance (metres) ─────────────────────────────────
inline double haversine_m(double lat1, double lon1, double lat2, double lon2) noexcept {
    constexpr double PI      = 3.14159265358979323846;
    constexpr double DEG2RAD = PI / 180.0;
    const double dlat = (lat2 - lat1) * DEG2RAD;
    const double dlon = (lon2 - lon1) * DEG2RAD;
    const double a = std::sin(dlat * 0.5) * std::sin(dlat * 0.5)
                   + std::cos(lat1 * DEG2RAD) * std::cos(lat2 * DEG2RAD)
                   * std::sin(dlon * 0.5) * std::sin(dlon * 0.5);
    return 2.0 * EARTH_RADIUS_M * std::asin(std::sqrt(a));
}

inline double haversine_m(LatLon a, LatLon b) noexcept {
    return haversine_m(a.lat, a.lon, b.lat, b.lon);
}

// ── Convert distance + speed → travel time in centiseconds ─────────────────
//   speed_kmh: km/h  |  dist_m: metres
inline Weight travel_time_cs(double dist_m, double speed_kmh) noexcept {
    if (speed_kmh <= 0.0) speed_kmh = 30.0;
    const double seconds = (dist_m / 1000.0) / speed_kmh * 3600.0;
    return static_cast<Weight>(seconds * 100.0);   // centiseconds
}

// ── Default speed lookup by OSM highway type ─────────────────────────────────
inline double default_speed_kmh(std::string_view highway) noexcept {
    if (highway == "motorway")       return 120.0;
    if (highway == "motorway_link")  return  80.0;
    if (highway == "trunk")          return 100.0;
    if (highway == "trunk_link")     return  60.0;
    if (highway == "primary")        return  80.0;
    if (highway == "primary_link")   return  60.0;
    if (highway == "secondary")      return  60.0;
    if (highway == "secondary_link") return  50.0;
    if (highway == "tertiary")       return  50.0;
    if (highway == "tertiary_link")  return  40.0;
    if (highway == "residential")    return  30.0;
    if (highway == "living_street")  return  10.0;
    if (highway == "unclassified")   return  40.0;
    if (highway == "road")           return  40.0;
    if (highway == "service")        return  20.0;
    return 30.0; // fallback
}

// ── Parse maxspeed tag (handles "50", "50 mph", "DE:urban", etc.) ────────────
inline double parse_maxspeed(std::string_view tag) noexcept {
    if (tag.empty()) return 0.0;
    // Try direct numeric parse
    char* end{};
    double val = std::strtod(tag.data(), &end);
    if (end != tag.data() && val > 0.0) {
        std::string_view rest(end);
        // skip leading spaces
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        if (rest.starts_with("mph"))
            return val * 1.60934;
        return val; // assume km/h
    }
    // Country-coded values — map common ones
    if (tag == "DE:urban"  || tag == "FR:urban"  || tag == "urban")   return 50.0;
    if (tag == "DE:rural"  || tag == "FR:rural"  || tag == "rural")   return 100.0;
    if (tag == "DE:motorway" || tag == "motorway")                     return 130.0;
    if (tag == "DE:living_street" || tag == "living_street")           return 7.0;
    if (tag == "walk" || tag == "DE:walk")                             return 5.0;
    return 0.0; // unknown — caller uses highway-type default
}

} // namespace routing::geo
