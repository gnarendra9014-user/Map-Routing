#pragma once
// ============================================================================
//  graph.hpp — CSR (Compressed Sparse Row) directed graph
//
//  Memory layout:
//
//    head[v]  .. head[v+1]-1  → range of adj[] entries for node v
//    adj[i].to                → destination node
//    adj[i].weight            → travel time in centiseconds
//    adj[i].speed_kmh         → edge speed (for heuristic calibration)
//
//  We store both a forward graph and a backward graph (same edges, reversed)
//  to support bidirectional algorithms without any extra memory gymnastics.
// ============================================================================
#include "types.hpp"
#include "geo.hpp"
#include "osm_parser.hpp"
#include <span>
#include <vector>

namespace routing {

// ── CSR adjacency entry ───────────────────────────────────────────────────────
struct AdjEntry {
    NodeId to{INVALID_NODE};
    Weight weight{INF_WEIGHT};   // centiseconds
    float  speed_kmh{50.0f};     // used by A* heuristic calibration
};

// ── Full graph ────────────────────────────────────────────────────────────────
class Graph {
public:
    Graph() = default;

    // Build from raw OsmData (runs compaction + CSR construction)
    static Graph build(const OsmData& data);

    // ── Accessors ─────────────────────────────────────────────────────────────
    uint32_t num_nodes() const noexcept { return static_cast<uint32_t>(coords_.size()); }
    uint64_t num_edges() const noexcept { return fwd_adj_.size(); }

    // Forward neighbours of node v
    std::span<const AdjEntry> fwd(NodeId v) const noexcept {
        return { fwd_adj_.data() + fwd_head_[v],
                 fwd_head_[v + 1] - fwd_head_[v] };
    }

    // Backward neighbours of node v (i.e. who points TO v)
    std::span<const AdjEntry> bwd(NodeId v) const noexcept {
        return { bwd_adj_.data() + bwd_head_[v],
                 bwd_head_[v + 1] - bwd_head_[v] };
    }

    // Coordinate of node v
    LatLon coord(NodeId v) const noexcept { return coords_[v]; }

    // Maximum speed across entire graph (used for admissible A* heuristic)
    double max_speed_kmh() const noexcept { return max_speed_kmh_; }

    // Haversine distance from v to t in centiseconds (A* lower bound)
    Weight heuristic(NodeId v, NodeId t) const noexcept {
        const double dist = geo::haversine_m(coords_[v], coords_[t]);
        return geo::travel_time_cs(dist, max_speed_kmh_);
    }

    // ── Serialization (binary) ────────────────────────────────────────────────
    void save(const std::string& path) const;
    static Graph load(const std::string& path);

private:
    // Forward CSR
    std::vector<EdgeId>   fwd_head_;  // size: N+1
    std::vector<AdjEntry> fwd_adj_;   // size: M

    // Backward CSR
    std::vector<EdgeId>   bwd_head_;  // size: N+1
    std::vector<AdjEntry> bwd_adj_;   // size: M

    // Node coordinates (compact IDs)
    std::vector<LatLon>   coords_;

    double max_speed_kmh_{130.0};
};

} // namespace routing
