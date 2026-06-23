#pragma once
// ============================================================================
//  osm_parser.hpp — Two-pass libosmium PBF parser
//
//  Pass 1: Walk all Ways, record which highway types we want, collect the
//          set of required NodeIds and per-way metadata.
//  Pass 2: Walk all Nodes, store lat/lon for required IDs only.
//
//  Output: OsmData struct ready for graph construction.
// ============================================================================
#include "types.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace routing {

// Raw edge from OSM (uses OSM node IDs, not compact IDs)
struct OsmEdge {
    int64_t  from{0};
    int64_t  to{0};
    double   speed_kmh{50.0};
    bool     oneway{false};   // true → only from→to is valid
};

// Raw node from OSM
struct OsmNode {
    int64_t  id{0};
    double   lat{0.0};
    double   lon{0.0};
};

// Complete raw data extracted from the PBF file
struct OsmData {
    std::vector<OsmNode> nodes;  // only road-connected nodes
    std::vector<OsmEdge> edges;  // directed edges (oneway already split)

    // Lookup: OSM node ID → index in nodes[]
    std::unordered_map<int64_t, size_t> node_index;
};

// ── Parser options ────────────────────────────────────────────────────────────
struct ParserOptions {
    // Highway tags to include (drivable roads)
    bool include_motorway     {true};
    bool include_trunk        {true};
    bool include_primary      {true};
    bool include_secondary    {true};
    bool include_tertiary     {true};
    bool include_residential  {true};
    bool include_unclassified {true};
    bool include_living_street{true};
    bool include_service      {false}; // parking aisles etc. — off by default
    bool include_road         {true};
};

// ── Main entry point ──────────────────────────────────────────────────────────
//
// Parses `pbf_path` in two passes and populates `out`.
// Returns number of edges extracted.
//
size_t parse_osm_pbf(const std::string& pbf_path,
                     OsmData&           out,
                     const ParserOptions& opts = {});

} // namespace routing
