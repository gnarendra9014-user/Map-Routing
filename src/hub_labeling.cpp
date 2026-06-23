// ============================================================================
//  hub_labeling.cpp — Hub Label construction and query
// ============================================================================
#include "hub_labeling.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <vector>

namespace routing {

// ── Construction ──────────────────────────────────────────────────────────────
//
//  We process nodes in decreasing rank order (highest-rank node first).
//  For each node v, we compute labels by "pulling" from already-labeled
//  higher-rank neighbors.
//
//  Forward label L_f[v]:
//    Run a Dijkstra from v on the UPWARD forward CH graph.
//    For each reached node h: add (h, dist) to L_f[v] unless it's pruned.
//
//  Backward label L_b[v]:
//    Same, on the UPWARD backward CH graph (= downward of forward graph).
//
//  Pruning condition: hub h is NOT added to L_f[v] if
//    hub_query(L_f[v] so far, L_b[h]) ≤ dist(v,h)
//  i.e. there's already a path through an earlier hub that's as good.

HubLabels build_hub_labels(const CHGraph& ch) {
    const uint32_t N = ch.num_nodes;
    HubLabels hl;
    hl.num_nodes = N;
    hl.L_f.resize(N);
    hl.L_b.resize(N);

    // Build rank-to-node ordering (decreasing rank = process highest-rank first)
    std::vector<NodeId> order(N);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](NodeId a, NodeId b) {
        return ch.nodes[a].rank > ch.nodes[b].rank;
    });

    std::cerr << "[HL] Building hub labels for " << N << " nodes...\n";

    // ── Forward labels ────────────────────────────────────────────────────────
    for (uint32_t idx = 0; idx < N; ++idx) {
        NodeId v = order[idx];
        if (idx % 100000 == 0)
            std::cerr << "[HL] Fwd labels: " << idx << "/" << N << "\r";

        // Dijkstra from v on upward forward CH graph
        struct E { Weight d; NodeId n;
                   bool operator>(const E& o) const { return d > o.d; } };
        std::priority_queue<E, std::vector<E>, std::greater<E>> pq;
        std::vector<Weight> dist(N, INF_WEIGHT);
        dist[v] = 0;
        pq.push({0, v});

        while (!pq.empty()) {
            auto [d, u] = pq.top(); pq.pop();
            if (d > dist[u]) continue;

            // Prune: check if this (u, d) is dominated by existing labels
            // hub_query_partial: intersect hl.L_f[v] built so far with hl.L_b[u]
            Weight best = INF_WEIGHT;
            {
                // L_f[v] is built incrementally (sorted by hub ID for intersection)
                // For simplicity, linear scan here; production code would sort.
                for (const auto& fe : hl.L_f[v]) {
                    for (const auto& be : hl.L_b[u]) {
                        if (fe.hub == be.hub) {
                            best = std::min(best, fe.dist + be.dist);
                        }
                    }
                }
            }
            if (best <= d) continue; // pruned

            // Add u as a hub for v
            hl.L_f[v].push_back({u, d});

            for (const auto& e : ch.upward[u]) {
                if (ch.nodes[e.to].rank <= ch.nodes[u].rank) continue;
                Weight nd = d + e.weight;
                if (nd < dist[e.to]) {
                    dist[e.to] = nd;
                    pq.push({nd, e.to});
                }
            }
        }
    }
    std::cerr << "\n[HL] Forward labels done.\n";

    // ── Backward labels ───────────────────────────────────────────────────────
    for (uint32_t idx = 0; idx < N; ++idx) {
        NodeId v = order[idx];
        if (idx % 100000 == 0)
            std::cerr << "[HL] Bwd labels: " << idx << "/" << N << "\r";

        struct E { Weight d; NodeId n;
                   bool operator>(const E& o) const { return d > o.d; } };
        std::priority_queue<E, std::vector<E>, std::greater<E>> pq;
        std::vector<Weight> dist(N, INF_WEIGHT);
        dist[v] = 0;
        pq.push({0, v});

        while (!pq.empty()) {
            auto [d, u] = pq.top(); pq.pop();
            if (d > dist[u]) continue;

            // Prune against forward labels of u
            Weight best = INF_WEIGHT;
            for (const auto& fe : hl.L_f[u]) {
                for (const auto& be : hl.L_b[v]) {
                    if (fe.hub == be.hub)
                        best = std::min(best, fe.dist + be.dist);
                }
            }
            if (best <= d) continue;

            hl.L_b[v].push_back({u, d});

            for (const auto& e : ch.downward[u]) {
                if (ch.nodes[e.to].rank <= ch.nodes[u].rank) continue;
                Weight nd = d + e.weight;
                if (nd < dist[e.to]) {
                    dist[e.to] = nd;
                    pq.push({nd, e.to});
                }
            }
        }
    }
    std::cerr << "\n[HL] Backward labels done.\n";

    // Sort labels by hub for fast intersection
    for (NodeId v = 0; v < N; ++v) {
        std::sort(hl.L_f[v].begin(), hl.L_f[v].end(),
                  [](const HubEntry& a, const HubEntry& b){ return a.hub < b.hub; });
        std::sort(hl.L_b[v].begin(), hl.L_b[v].end(),
                  [](const HubEntry& a, const HubEntry& b){ return a.hub < b.hub; });
    }

    return hl;
}

// ── Query ─────────────────────────────────────────────────────────────────────
// Sorted intersection — O(|L_f| + |L_b|)
Weight hub_query(const HubLabels& hl, NodeId src, NodeId dst) {
    const auto& lf = hl.L_f[src];
    const auto& lb = hl.L_b[dst];

    Weight best = INF_WEIGHT;
    size_t i = 0, j = 0;

    while (i < lf.size() && j < lb.size()) {
        if (lf[i].hub == lb[j].hub) {
            Weight cand = lf[i].dist + lb[j].dist;
            if (cand < best) best = cand;
            ++i; ++j;
        } else if (lf[i].hub < lb[j].hub) {
            ++i;
        } else {
            ++j;
        }
    }
    return best;
}

RouteResult hub_query_full(const HubLabels& hl, const CHGraph& ch,
                           NodeId src, NodeId dst)
{
    Weight cost = hub_query(hl, src, dst);
    if (cost == INF_WEIGHT) return {};

    // For path reconstruction, fall back to CH query
    RouteResult r = ch_query(ch, src, dst);
    // Override cost with HL result (more accurate, same value)
    r.cost = cost;
    return r;
}

// ── Serialization ─────────────────────────────────────────────────────────────
static constexpr uint32_t HL_MAGIC = 0x4C480001u; // 'LH\x00\x01'

void save_hub_labels(const HubLabels& hl, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write HL file");
    uint32_t magic = 0x4C480001u;
    uint32_t N     = hl.num_nodes;
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&N),     4);

    auto write_labels = [&](const std::vector<std::vector<HubEntry>>& L) {
        for (uint32_t v = 0; v < N; ++v) {
            uint32_t sz = static_cast<uint32_t>(L[v].size());
            f.write(reinterpret_cast<const char*>(&sz), 4);
            f.write(reinterpret_cast<const char*>(L[v].data()),
                    sz * sizeof(HubEntry));
        }
    };
    write_labels(hl.L_f);
    write_labels(hl.L_b);
}

HubLabels load_hub_labels(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot read HL file");
    uint32_t magic{}, N{};
    f.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != 0x4C480001u) throw std::runtime_error("Bad HL magic");
    f.read(reinterpret_cast<char*>(&N), 4);

    HubLabels hl;
    hl.num_nodes = N;
    hl.L_f.resize(N);
    hl.L_b.resize(N);

    auto read_labels = [&](std::vector<std::vector<HubEntry>>& L) {
        for (uint32_t v = 0; v < N; ++v) {
            uint32_t sz{};
            f.read(reinterpret_cast<char*>(&sz), 4);
            L[v].resize(sz);
            f.read(reinterpret_cast<char*>(L[v].data()), sz * sizeof(HubEntry));
        }
    };
    read_labels(hl.L_f);
    read_labels(hl.L_b);
    return hl;
}

} // namespace routing
