#pragma once
// ============================================================================
//  alt.hpp — ALT: A* with Landmarks and Triangle inequality
//
//  Heuristic derivation:
//    For landmark L, triangle inequality gives:
//      dist(u, t) ≥ dist(L, t) - dist(L, u)    (L is "in front of" t)
//      dist(u, t) ≥ dist(u, L) - dist(t, L)    (L is "behind" u)
//
//    So: h_ALT(u, t) = max over all landmarks L of
//          max(dist(L,t) - dist(L,u),  dist(u,L) - dist(t,L))
//
//    This is always ≥ Haversine/max_speed, so ALT dominates the simple heuristic.
//
//  Landmark selection: farthest-point sampling (greedy) gives good coverage.
// ============================================================================
#include "types.hpp"
#include "graph.hpp"
#include <string>
#include <vector>

namespace routing {

struct ALTData {
    uint32_t              num_nodes{0};
    uint32_t              num_landmarks{0};
    std::vector<NodeId>   landmarks;       // landmark node IDs

    // dist_from_L[l][v] = shortest path from landmarks[l] to v
    // dist_to_L[l][v]   = shortest path from v to landmarks[l]
    // (= from landmarks[l] in the reverse graph)
    std::vector<std::vector<Weight>> dist_from_L;
    std::vector<std::vector<Weight>> dist_to_L;
};

// ── Preprocessing ─────────────────────────────────────────────────────────────
// Selects `num_landmarks` via farthest-point sampling, runs Dijkstra from each.
ALTData build_alt(const Graph& g, uint32_t num_landmarks = 16, bool verbose = true);

// ── Heuristic ─────────────────────────────────────────────────────────────────
inline Weight alt_heuristic(const ALTData& alt, NodeId u, NodeId t) noexcept {
    Weight h = 0;
    for (uint32_t l = 0; l < alt.num_landmarks; ++l) {
        const Weight dLu = alt.dist_from_L[l][u];
        const Weight dLt = alt.dist_from_L[l][t];
        const Weight duL = alt.dist_to_L[l][u];
        const Weight dtL = alt.dist_to_L[l][t];

        // Forward: L → t → u  → h ≥ d(L,t) - d(L,u)
        if (dLt != INF_WEIGHT && dLu != INF_WEIGHT && dLt > dLu)
            h = std::max(h, dLt - dLu);

        // Backward: u → L ← t  → h ≥ d(u,L) - d(t,L)
        if (duL != INF_WEIGHT && dtL != INF_WEIGHT && duL > dtL)
            h = std::max(h, duL - dtL);
    }
    return h;
}

// ── A* query using ALT heuristic ─────────────────────────────────────────────
RouteResult alt_query(const Graph& g, const ALTData& alt,
                      NodeId src, NodeId dst);

// ── Serialization ─────────────────────────────────────────────────────────────
void save_alt(const ALTData& alt, const std::string& path);
ALTData load_alt(const std::string& path);

} // namespace routing
