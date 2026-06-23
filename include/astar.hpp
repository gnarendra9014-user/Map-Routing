#pragma once
// ============================================================================
//  astar.hpp — A* with Haversine lower-bound heuristic
//
//  Key design: f(v) = g(v) + h(v)
//    g(v) = best known travel-time cost from src to v (centiseconds)
//    h(v) = haversine_distance(v, target) / max_speed   (admissible lower bound)
//
//  The heuristic is consistent (satisfies triangle inequality) because:
//    h(u) ≤ w(u,v) + h(v)  holds when the heuristic is the true distance
//    on an unconstrained plane — Haversine / max_speed satisfies this.
//
//  With a consistent heuristic, A* settles each node at most once (like
//  Dijkstra), so no re-opening is needed.
// ============================================================================
#include "types.hpp"
#include "graph.hpp"
#include <vector>
#include <unordered_map>

namespace routing {

class AStar {
public:
    explicit AStar(const Graph& g);

    // Point-to-point query.
    RouteResult query(NodeId src, NodeId dst, const RoutingOptions& opts = {});

    // Stats from last query (useful for benchmarking)
    uint32_t nodes_expanded() const noexcept { return nodes_expanded_; }
    uint32_t nodes_settled()  const noexcept { return nodes_settled_;  }

private:
    const Graph&          graph_;
    std::vector<Weight>   cost_;     // cost_[v] = best g(v) from source
    std::vector<NodeId>   prev_;
    std::vector<NodeId>   touched_;

    uint32_t nodes_expanded_{0};
    uint32_t nodes_settled_ {0};

    void reset();
    RouteResult reconstruct(NodeId src, NodeId dst) const;
};

} // namespace routing
