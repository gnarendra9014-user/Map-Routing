// ============================================================================
//  dijkstra.cpp
// ============================================================================
#include "dijkstra.hpp"
#include <queue>
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace routing {

// ── Priority queue entry ──────────────────────────────────────────────────────
struct PQEntry {
    Weight dist;
    NodeId node;
    // min-heap: smallest dist first
    bool operator>(const PQEntry& o) const noexcept { return dist > o.dist; }
};

using MinHeap = std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>>;

// ── Constructor ───────────────────────────────────────────────────────────────
Dijkstra::Dijkstra(const Graph& g)
    : g_(g)
    , dist_(g.num_nodes(), INF_WEIGHT)
    , prev_(g.num_nodes(), INVALID_NODE)
{}

void Dijkstra::reset() {
    for (NodeId v : settled_) {
        dist_[v] = INF_WEIGHT;
        prev_[v] = INVALID_NODE;
    }
    settled_.clear();
}

// ── Path reconstruction ───────────────────────────────────────────────────────
RouteResult Dijkstra::reconstruct(NodeId src, NodeId dst) const {
    RouteResult r;
    if (dist_[dst] == INF_WEIGHT) return r;

    r.cost  = dist_[dst];
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
            return 0.5;
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

// ── Point-to-point query ──────────────────────────────────────────────────────
RouteResult Dijkstra::query(NodeId src, NodeId dst, const RoutingOptions& opts) {
    reset();
    if (src >= g_.num_nodes() || dst >= g_.num_nodes())
        throw std::out_of_range("Node ID out of range");

    MinHeap pq;
    dist_[src] = 0;
    settled_.push_back(src);
    pq.push({0, src});

    std::vector<NodeId> visited;

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();

        if (d > dist_[u]) continue;

        visited.push_back(u);

        if (u == dst) break;

        for (const auto& e : g_.fwd(u)) {
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
                multiplier *= get_edge_traffic_factor(g_, u, e, opts.time_of_day);
            }

            if (opts.profile != "fastest" && !opts.profile.empty()) {
                multiplier *= get_profile_factor(g_, u, e, opts.profile);
            }

            Weight nd = dist_[u] + static_cast<Weight>(e.weight * multiplier);
            if (nd < dist_[e.to]) {
                if (dist_[e.to] == INF_WEIGHT)   // first time touched, push before updating
                    settled_.push_back(e.to);
                dist_[e.to] = nd;
                prev_[e.to] = u;
                pq.push({nd, e.to});
            }
        }
    }

    RouteResult r = reconstruct(src, dst);
    r.visited = std::move(visited);
    return r;
}

// ── One-to-all ────────────────────────────────────────────────────────────────
std::vector<Weight> Dijkstra::one_to_all(NodeId src) {
    reset();

    MinHeap pq;
    dist_[src] = 0;
    settled_.push_back(src);
    pq.push({0, src});

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist_[u]) continue;

        for (const auto& e : g_.fwd(u)) {
            Weight nd = dist_[u] + e.weight;
            if (nd < dist_[e.to]) {
                if (dist_[e.to] == INF_WEIGHT) settled_.push_back(e.to);
                dist_[e.to] = nd;
                prev_[e.to] = u;
                pq.push({nd, e.to});
            }
        }
    }

    // Return a copy (the internal array will be reset on next call)
    return std::vector<Weight>(dist_.begin(), dist_.end());
}

// ── Witness search (for CH) ───────────────────────────────────────────────────
// Runs a limited Dijkstra from src. Sets dist_ for all reached nodes.
// Returns number of nodes settled.
uint32_t Dijkstra::witness_search(NodeId src, Weight cost_limit, uint32_t max_hops) {
    reset();

    MinHeap pq;
    dist_[src] = 0;
    settled_.push_back(src);
    pq.push({0, src});

    uint32_t hops = 0;

    while (!pq.empty() && hops < max_hops) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist_[u]) continue;
        if (d > cost_limit) break;

        ++hops;

        for (const auto& e : g_.fwd(u)) {
            Weight nd = dist_[u] + e.weight;
            if (nd < dist_[e.to]) {
                if (dist_[e.to] == INF_WEIGHT) settled_.push_back(e.to);
                dist_[e.to] = nd;
                pq.push({nd, e.to});
            }
        }
    }

    return hops;
}

} // namespace routing
