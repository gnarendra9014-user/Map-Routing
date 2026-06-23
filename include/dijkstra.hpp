#pragma once
// ============================================================================
//  dijkstra.hpp — Classic single-source Dijkstra with lazy deletion heap
// ============================================================================
#include "types.hpp"
#include "graph.hpp"
#include <vector>
#include <optional>

namespace routing {

class Dijkstra {
public:
    explicit Dijkstra(const Graph& g);

    // Shortest path from src to dst.
    RouteResult query(NodeId src, NodeId dst, const RoutingOptions& opts = {});

    // One-to-all shortest distances (no path reconstruction).
    // Returns dist[v] for all v. Useful for CH preprocessing.
    std::vector<Weight> one_to_all(NodeId src);

    // Settle up to `max_hops` levels (for witness searches in CH).
    // Fills dist_ in-place. Returns nodes settled.
    uint32_t witness_search(NodeId src, Weight cost_limit, uint32_t max_hops);

    // Read dist[v] after witness_search (INF_WEIGHT if unreachable).
    Weight dist(NodeId v) const noexcept { return dist_[v]; }

private:
    const Graph&          g_;
    std::vector<Weight>   dist_;
    std::vector<NodeId>   prev_;
    std::vector<NodeId>   settled_; // for fast reset

    void reset();
    RouteResult reconstruct(NodeId src, NodeId dst) const;
};

} // namespace routing
