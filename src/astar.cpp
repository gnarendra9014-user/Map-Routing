// ============================================================================
//  astar.cpp — A* shortest path
// ============================================================================
#include "astar.hpp"
#include <queue>
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace routing {

// ── PQ entry ──────────────────────────────────────────────────────────────────
struct AStarEntry {
    Weight f;      // f = g + h (priority key)
    Weight g;      // true cost so far
    NodeId node;
    bool operator>(const AStarEntry& o) const noexcept { return f > o.f; }
};

using MinHeap = std::priority_queue<AStarEntry,
                                    std::vector<AStarEntry>,
                                    std::greater<AStarEntry>>;

// ── Constructor ───────────────────────────────────────────────────────────────
AStar::AStar(const Graph& g)
    : graph_(g)
    , cost_(g.num_nodes(), INF_WEIGHT)
    , prev_(g.num_nodes(), INVALID_NODE)
{}

void AStar::reset() {
    for (NodeId v : touched_) {
        cost_[v] = INF_WEIGHT;
        prev_[v] = INVALID_NODE;
    }
    touched_.clear();
    nodes_expanded_ = 0;
    nodes_settled_  = 0;
}

RouteResult AStar::reconstruct(NodeId /*src*/, NodeId dst) const {
    RouteResult r;
    if (cost_[dst] == INF_WEIGHT) return r;

    r.cost  = cost_[dst];
    r.found = true;

    NodeId v = dst;
    while (v != INVALID_NODE) {
        r.path.push_back(v);
        v = prev_[v];
    }
    std::reverse(r.path.begin(), r.path.end());
    return r;
}

static double get_edge_traffic_factor(const Graph& g, NodeId u, const AdjEntry& e, int time_of_day) {
    if (time_of_day < 0 || time_of_day >= 1440) return 1.0;
    
    double intensity = 0.0;
    if (time_of_day >= 480 && time_of_day <= 570) {
        double diff = std::abs(time_of_day - 525);
        intensity = 1.0 - (diff / 45.0);
    } else if (time_of_day >= 1020 && time_of_day <= 1110) {
        double diff = std::abs(time_of_day - 1065);
        intensity = 1.0 - (diff / 45.0);
    }
    
    if (intensity <= 0.0) return 1.0;

    LatLon c = g.coord(u);
    bool is_arterial = (e.speed_kmh >= 50.0f);
    
    double dist_to_port = geo::haversine_m(c.lat, c.lon, 43.7347, 7.4214);
    double dist_to_casino = geo::haversine_m(c.lat, c.lon, 43.7396, 7.4273);
    
    bool is_congested_zone = (dist_to_port < 400.0) || (dist_to_casino < 400.0);
    
    if (is_arterial && is_congested_zone) {
        return 1.0 + intensity * 1.5;
    } else if (is_arterial) {
        return 1.0 + intensity * 0.8;
    } else if (is_congested_zone) {
        return 1.0 + intensity * 0.5;
    }
    
    return 1.0;
}

static double get_profile_factor(const Graph& g, NodeId u, const AdjEntry& e, const std::string& profile) {
    if (profile == "scenic") {
        LatLon c = g.coord(u);
        double d1 = geo::haversine_m(c.lat, c.lon, 43.735, 7.422);
        double d2 = geo::haversine_m(c.lat, c.lon, 43.746, 7.432);
        double d3 = geo::haversine_m(c.lat, c.lon, 43.727, 7.422);
        
        if (d1 < 250.0 || d2 < 250.0 || d3 < 200.0) {
            return 0.5; // coastal discount
        }
        return 1.0;
    }
    
    if (profile == "green") {
        double factor = 1.0;
        if (e.speed_kmh >= 70.0f) {
            factor = 4.0;
        } else if (e.speed_kmh >= 50.0f) {
            factor = 2.0;
        } else if (e.speed_kmh <= 30.0f) {
            factor = 0.6;
        }
        
        LatLon c = g.coord(u);
        double d_park1 = geo::haversine_m(c.lat, c.lon, 43.7377, 7.4149);
        double d_park2 = geo::haversine_m(c.lat, c.lon, 43.7397, 7.4267);
        double d_park3 = geo::haversine_m(c.lat, c.lon, 43.7303, 7.4241);
        
        if (d_park1 < 150.0 || d_park2 < 150.0 || d_park3 < 150.0) {
            factor *= 0.5;
        }
        return factor;
    }
    
    return 1.0;
}

// ── Query ─────────────────────────────────────────────────────────────────────
RouteResult AStar::query(NodeId src, NodeId dst, const RoutingOptions& opts) {
    reset();
    if (src >= graph_.num_nodes() || dst >= graph_.num_nodes())
        throw std::out_of_range("Node ID out of range");

    if (src == dst) {
        RouteResult r;
        r.cost = 0; r.found = true; r.path = {src};
        r.visited = {src};
        return r;
    }

    MinHeap pq;
    cost_[src] = 0;
    touched_.push_back(src);
    const Weight h0 = (opts.profile == "fastest" && opts.time_of_day < 0) ? graph_.heuristic(src, dst) : 0;
    pq.push({h0, 0, src});

    std::vector<NodeId> visited;

    while (!pq.empty()) {
        auto [f, g, u] = pq.top(); pq.pop();

        if (g > cost_[u]) continue;

        visited.push_back(u);
        ++nodes_settled_;

        if (u == dst) break;

        ++nodes_expanded_;

        for (const auto& e : graph_.fwd(u)) {
            // Check avoidance zone
            if (!opts.node_avoided.empty() && opts.node_avoided[e.to]) {
                continue;
            }

            double multiplier = 1.0;
            if (!opts.edge_penalties.empty()) {
                uint64_t key = (static_cast<uint64_t>(u) << 32) | e.to;
                auto it = opts.edge_penalties.find(key);
                if (it != opts.edge_penalties.end()) {
                    multiplier *= it->second;
                }
            }

            if (opts.time_of_day >= 0) {
                multiplier *= get_edge_traffic_factor(graph_, u, e, opts.time_of_day);
            }

            if (opts.profile != "fastest" && !opts.profile.empty()) {
                multiplier *= get_profile_factor(graph_, u, e, opts.profile);
            }

            const Weight ng = cost_[u] + static_cast<Weight>(e.weight * multiplier);
            if (ng < cost_[e.to]) {
                if (cost_[e.to] == INF_WEIGHT)
                    touched_.push_back(e.to);
                cost_[e.to] = ng;
                prev_[e.to] = u;
                // If profile is not default or traffic is active, heuristic is disabled (h=0) to ensure correctness
                const Weight h = (opts.profile == "fastest" && opts.time_of_day < 0) ? graph_.heuristic(e.to, dst) : 0;
                pq.push({ng + h, ng, e.to});
            }
        }
    }

    RouteResult r = reconstruct(src, dst);
    r.visited = std::move(visited);
    return r;
}

} // namespace routing
