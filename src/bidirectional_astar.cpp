// ============================================================================
//  bidirectional_astar.cpp
// ============================================================================
#include "bidirectional_astar.hpp"
#include "geo.hpp"

#include <cmath>
#include <queue>
#include <stdexcept>

namespace routing {

// ── PQ entry ──────────────────────────────────────────────────────────────────
struct BiEntry {
    Weight f;
    Weight g;
    NodeId node;
    bool operator>(const BiEntry& o) const noexcept { return f > o.f; }
};
using MinHeap = std::priority_queue<BiEntry, std::vector<BiEntry>, std::greater<BiEntry>>;

// ── Constructor ───────────────────────────────────────────────────────────────
BidirectionalAStar::BidirectionalAStar(const Graph& g)
    : g_(g)
    , dist_f_(g.num_nodes(), INF_WEIGHT)
    , dist_b_(g.num_nodes(), INF_WEIGHT)
    , prev_f_(g.num_nodes(), INVALID_NODE)
    , prev_b_(g.num_nodes(), INVALID_NODE)
{}

void BidirectionalAStar::reset() {
    for (NodeId v : touched_f_) {
        dist_f_[v] = INF_WEIGHT;
        prev_f_[v] = INVALID_NODE;
    }
    for (NodeId v : touched_b_) {
        dist_b_[v] = INF_WEIGHT;
        prev_b_[v] = INVALID_NODE;
    }
    touched_f_.clear();
    touched_b_.clear();
    fwd_settled_ = 0;
    bwd_settled_ = 0;
}

// ── Path reconstruction ───────────────────────────────────────────────────────
RouteResult BidirectionalAStar::reconstruct(
    NodeId src, NodeId dst, NodeId meeting) const
{
    RouteResult r;
    if (meeting == INVALID_NODE) return r;

    r.cost  = dist_f_[meeting] + dist_b_[meeting];
    r.found = true;

    // Forward: src → meeting
    std::vector<NodeId> fwd_path;
    for (NodeId v = meeting; v != INVALID_NODE; v = prev_f_[v])
        fwd_path.push_back(v);
    std::reverse(fwd_path.begin(), fwd_path.end());

    // Backward: meeting → dst  (prev_b_ stores the backward predecessor,
    //                            i.e. successor in the forward direction)
    std::vector<NodeId> bwd_path;
    for (NodeId v = prev_b_[meeting]; v != INVALID_NODE; v = prev_b_[v])
        bwd_path.push_back(v);

    r.path = std::move(fwd_path);
    r.path.insert(r.path.end(), bwd_path.begin(), bwd_path.end());

    return r;
}

// ── Query ─────────────────────────────────────────────────────────────────────
RouteResult BidirectionalAStar::query(NodeId src, NodeId dst) {
    reset();

    if (src >= g_.num_nodes() || dst >= g_.num_nodes())
        throw std::out_of_range("Node ID out of range");

    if (src == dst) {
        RouteResult r; r.cost = 0; r.found = true; r.path = {src};
        r.visited = {src};
        return r;
    }

    // ── Haversine lower bound s→t (used in termination criterion) ─────────────
    const Weight h_st = g_.heuristic(src, dst);

    // ── Initialise ────────────────────────────────────────────────────────────
    MinHeap pq_f, pq_b;

    auto touch_f = [&](NodeId v) {
        if (dist_f_[v] == INF_WEIGHT) touched_f_.push_back(v);
    };
    auto touch_b = [&](NodeId v) {
        if (dist_b_[v] == INF_WEIGHT) touched_b_.push_back(v);
    };

    touch_f(src); dist_f_[src] = 0;
    touch_b(dst); dist_b_[dst] = 0;

    // Forward heuristic: h_f(v) = haversine(v, dst) / max_speed
    // Backward heuristic: h_b(v) = haversine(v, src) / max_speed
    pq_f.push({g_.heuristic(src, dst), 0, src});
    pq_b.push({g_.heuristic(dst, src), 0, dst});

    Weight   mu          = INF_WEIGHT;   // best complete path found
    NodeId   meeting     = INVALID_NODE;

    std::vector<NodeId> visited;

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (!pq_f.empty() && !pq_b.empty()) {

        // ── Termination criterion (Pohl/Kaindl) ───────────────────────────────
        // μ ≤ top_f.f + top_b.f - h(s,t)
        // Note: f-values already incorporate heuristic, so this is correct.
        const Weight f_top_f = pq_f.top().f;
        const Weight f_top_b = pq_b.top().f;

        // Guard against underflow (unsigned arithmetic)
        if (h_st <= f_top_f + f_top_b) {
            if (mu <= f_top_f + f_top_b - h_st) break;
        } else {
            if (mu <= 0) break;  // degenerate: src==dst handled above
        }

        // ── Choose which frontier to expand ───────────────────────────────────
        // Expand the side with the smaller f-value (balanced strategy)
        bool expand_forward = (f_top_f <= f_top_b);

        if (expand_forward) {
            auto [f, g, u] = pq_f.top(); pq_f.pop();
            if (g > dist_f_[u]) continue;   // stale
            visited.push_back(u);
            ++fwd_settled_;

            for (const auto& e : g_.fwd(u)) {
                const Weight ng = dist_f_[u] + e.weight;
                if (ng < dist_f_[e.to]) {
                    touch_f(e.to);
                    dist_f_[e.to] = ng;
                    prev_f_[e.to] = u;
                    pq_f.push({ng + g_.heuristic(e.to, dst), ng, e.to});

                    // Update μ if this node was reached by the backward search
                    if (dist_b_[e.to] != INF_WEIGHT) {
                        const Weight cand = ng + dist_b_[e.to];
                        if (cand < mu) {
                            mu      = cand;
                            meeting = e.to;
                        }
                    }
                }
            }
        } else {
            auto [f, g, u] = pq_b.top(); pq_b.pop();
            if (g > dist_b_[u]) continue;   // stale
            visited.push_back(u);
            ++bwd_settled_;

            // Backward search uses the reversed graph (bwd() edges)
            for (const auto& e : g_.bwd(u)) {
                const Weight ng = dist_b_[u] + e.weight;
                if (ng < dist_b_[e.to]) {
                    touch_b(e.to);
                    dist_b_[e.to] = ng;
                    prev_b_[e.to] = u;
                    pq_b.push({ng + g_.heuristic(e.to, src), ng, e.to});

                    // Update μ if this node was reached by the forward search
                    if (dist_f_[e.to] != INF_WEIGHT) {
                        const Weight cand = dist_f_[e.to] + ng;
                        if (cand < mu) {
                            mu      = cand;
                            meeting = e.to;
                        }
                    }
                }
            }
        }
    }

    RouteResult r = reconstruct(src, dst, meeting);
    r.visited = std::move(visited);
    return r;
}

} // namespace routing
