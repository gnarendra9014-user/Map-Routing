#pragma once
// ============================================================================
//  ch.hpp — Contraction Hierarchies (preprocessing + query)
//
//  Preprocessing
//  ─────────────
//  Nodes are contracted in order of "importance". The importance of a node u is:
//
//    importance(u) = edge_difference(u)       ← shortcuts needed - edges removed
//                  + contracted_neighbors(u)   ← how many neighbors already contracted
//                  + depth(u)                  ← max depth of shortcut chain through u
//
//  We use a lazy priority queue: recompute importance just before contracting.
//
//  Contraction of node u:
//    For each pair (v → u → w):
//      Run a witness search from v (avoiding u) to w with limit w(v,u)+w(u,w).
//      If no witness exists → insert shortcut v→w.
//
//  Query (bidirectional Dijkstra on augmented CH graph)
//  ─────────────────────────────────────────────────────
//  Forward  search from src uses only UPWARD edges (to higher-rank nodes).
//  Backward search from dst uses only UPWARD edges in the reverse graph.
//  Result: min over all nodes v of dist_f[v] + dist_b[v].
//
//  Path unpacking: each shortcut edge records its "via" node; recursively
//  unpack until all edges are original edges.
// ============================================================================
#include "types.hpp"
#include "graph.hpp"
#include <functional>
#include <vector>

namespace routing {

// ── Augmented edge (original or shortcut) ─────────────────────────────────────
struct CHEdge {
    NodeId to       {INVALID_NODE};
    Weight weight   {INF_WEIGHT};
    NodeId via      {INVALID_NODE};  // INVALID_NODE → original edge
    bool   is_fwd   {true};          // edge direction for the base graph
};

// ── Per-node CH data ──────────────────────────────────────────────────────────
struct CHNode {
    uint32_t rank{0};         // contraction order (higher = more important)
    uint32_t depth{0};        // shortcut chain depth
    uint32_t contracted_neighbors{0};
};

// ── CH preprocessing result ───────────────────────────────────────────────────
struct CHGraph {
    // Augmented adjacency lists (upward only, sorted by rank)
    std::vector<std::vector<CHEdge>> upward;    // fwd upward edges from v
    std::vector<std::vector<CHEdge>> downward;  // bwd upward edges from v (=fwd upward of reverse)

    std::vector<CHNode>   nodes;
    uint32_t              num_nodes{0};
    uint64_t              num_shortcuts{0};

    // Coordinate access (from original graph)
    const Graph*          base_graph{nullptr};
};

// ── Main preprocessing ────────────────────────────────────────────────────────
CHGraph build_ch(const Graph& g,
                 bool         verbose = true);

// ── CH query ─────────────────────────────────────────────────────────────────
RouteResult ch_query(const CHGraph& ch, NodeId src, NodeId dst);

// ── Serialization ─────────────────────────────────────────────────────────────
void save_ch(const CHGraph& ch, const std::string& path);
CHGraph load_ch(const std::string& path, const Graph& base);

} // namespace routing
