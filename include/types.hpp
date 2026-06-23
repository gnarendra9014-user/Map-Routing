#pragma once
// ============================================================================
//  types.hpp — Shared primitive types for the entire routing engine
// ============================================================================
#include <cstdint>
#include <limits>
#include <vector>
#include <unordered_map>
#include <string>

namespace routing {

// Node identifier (compact, 0-based)
using NodeId = uint32_t;
// Edge identifier
using EdgeId = uint32_t;
// Weight in centiseconds (1/100 s) — integer fixed-point avoids float noise
using Weight = uint32_t;

constexpr NodeId   INVALID_NODE   = std::numeric_limits<NodeId>::max();
constexpr Weight   INF_WEIGHT     = std::numeric_limits<Weight>::max() / 2;
constexpr double   EARTH_RADIUS_M = 6'371'000.0;

// Geographic coordinate
struct LatLon {
    double lat{0.0};
    double lon{0.0};
};

// Query-time configuration options for pathfinding algorithms
struct RoutingOptions {
    std::unordered_map<uint64_t, double> edge_penalties{};
    std::vector<bool> node_avoided{};
    int time_of_day{-1}; // -1 = disabled, 0-1439 = minutes from midnight
    std::string profile{"fastest"}; // "fastest", "scenic", "green"
};

// A finished route
struct RouteResult {
    Weight           cost{INF_WEIGHT};   // total travel time in centiseconds
    std::vector<NodeId> path;            // ordered node IDs, source first
    std::vector<NodeId> visited;         // sequence of visited nodes in order of discovery/settling
    bool             found{false};
};

} // namespace routing

