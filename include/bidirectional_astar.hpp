#pragma once
// ============================================================================
//  bidirectional_astar.hpp — Bidirectional A* with correct termination
//
//  Algorithm overview
//  ──────────────────
//  Two simultaneous searches:
//    Forward  search from src (uses forward graph, heuristic h_f(v) = dist(v,t))
//    Backward search from dst (uses backward graph, heuristic h_b(v) = dist(v,s))
//
//  At each step we expand the frontier whose top has a smaller f-value
//  (the "balanced" expansion strategy).
//
//  Termination condition (Pohl/Kaindl criterion):
//  ──────────────────────────────────────────────
//  Let μ = best complete path found through any node touched by both searches.
//  Let f_min_f = f-value of the top of the forward  heap.
//  Let f_min_b = f-value of the top of the backward heap.
//
//  We cannot stop simply because the two frontiers "meet" — the meeting node
//  might not lie on the optimal path.  The correct criterion is:
//
//    STOP when  μ  ≤  f_min_f + f_min_b - h(s,t)
//
//  where h(s,t) is the haversine lower bound from source to target.
//  This guarantees μ is the true shortest path.
//
//  Reference: Ikeda et al. (1994), "A Fast Algorithm for Finding Better Routes
//             by AI Search Techniques", IEEE ITSC.
// ============================================================================
#include "types.hpp"
#include "graph.hpp"
#include <vector>

namespace routing {

class BidirectionalAStar {
public:
    explicit BidirectionalAStar(const Graph& g);

    RouteResult query(NodeId src, NodeId dst);

    // Benchmarking stats
    uint32_t fwd_settled() const noexcept { return fwd_settled_; }
    uint32_t bwd_settled() const noexcept { return bwd_settled_; }

private:
    const Graph&          g_;
    std::vector<Weight>   dist_f_;   // g-values, forward  search
    std::vector<Weight>   dist_b_;   // g-values, backward search
    std::vector<NodeId>   prev_f_;
    std::vector<NodeId>   prev_b_;
    std::vector<NodeId>   touched_f_;
    std::vector<NodeId>   touched_b_;

    uint32_t fwd_settled_{0};
    uint32_t bwd_settled_{0};

    void reset();
    RouteResult reconstruct(NodeId src, NodeId dst, NodeId meeting) const;
};

} // namespace routing
