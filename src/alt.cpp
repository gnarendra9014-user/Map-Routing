// ============================================================================
//  alt.cpp — ALT preprocessing and query
// ============================================================================
#include "alt.hpp"
#include "dijkstra.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <queue>
#include <random>
#include <stdexcept>

namespace routing {

// ── Farthest-point landmark selection ────────────────────────────────────────
static std::vector<NodeId>
select_landmarks_farthest(const Graph& g, uint32_t K) {
    const uint32_t N = g.num_nodes();
    std::vector<NodeId> landmarks;
    landmarks.reserve(K);

    // Start with a random node
    std::mt19937 rng{42};
    NodeId seed = std::uniform_int_distribution<NodeId>{0, N - 1}(rng);
    landmarks.push_back(seed);

    // Distances to nearest landmark for each node
    std::vector<Weight> min_dist(N, INF_WEIGHT);

    Dijkstra dijk(g);

    for (uint32_t k = 0; k < K; ++k) {
        // Run Dijkstra from last selected landmark
        NodeId last = landmarks.back();
        auto d = dijk.one_to_all(last);

        // Update min_dist
        for (NodeId v = 0; v < N; ++v)
            min_dist[v] = std::min(min_dist[v], d[v]);

        if (k + 1 < K) {
            // Pick the node farthest from all landmarks so far
            NodeId farthest = 0;
            Weight farthest_d = 0;
            for (NodeId v = 0; v < N; ++v) {
                if (min_dist[v] > farthest_d && min_dist[v] != INF_WEIGHT) {
                    farthest_d = min_dist[v];
                    farthest   = v;
                }
            }
            landmarks.push_back(farthest);
        }

        std::cerr << "[ALT] Landmark " << (k + 1) << "/" << K
                  << " = node " << last << "\n";
    }

    return landmarks;
}

// ── Main preprocessing ────────────────────────────────────────────────────────
ALTData build_alt(const Graph& g, uint32_t num_landmarks, bool verbose) {
    const uint32_t N = g.num_nodes();
    ALTData alt;
    alt.num_nodes     = N;
    alt.num_landmarks = num_landmarks;

    if (verbose)
        std::cerr << "[ALT] Selecting " << num_landmarks << " landmarks...\n";

    alt.landmarks = select_landmarks_farthest(g, num_landmarks);

    alt.dist_from_L.resize(num_landmarks, std::vector<Weight>(N, INF_WEIGHT));
    alt.dist_to_L  .resize(num_landmarks, std::vector<Weight>(N, INF_WEIGHT));

    Dijkstra dijk(g);

    for (uint32_t l = 0; l < num_landmarks; ++l) {
        NodeId lm = alt.landmarks[l];

        if (verbose)
            std::cerr << "[ALT] Forward Dijkstra from landmark " << l << "\n";

        // dist_from_L[l] = Dijkstra on forward graph from lm
        alt.dist_from_L[l] = dijk.one_to_all(lm);

        if (verbose)
            std::cerr << "[ALT] Backward Dijkstra from landmark " << l << "\n";

        // dist_to_L[l] = Dijkstra on backward graph from lm
        // We simulate this by running Dijkstra on the reverse graph.
        // Since Graph stores bwd CSR, we create a transposed view.
        // Simple approach: run one_to_all on a backward version.
        // We reuse Dijkstra by temporarily reading bwd() edges.
        // For simplicity, we implement a backward Dijkstra inline here.
        {
            struct E { Weight d; NodeId n;
                       bool operator>(const E& o) const { return d > o.d; } };
            std::priority_queue<E, std::vector<E>, std::greater<E>> pq;
            auto& dist = alt.dist_to_L[l];
            dist.assign(N, INF_WEIGHT);
            dist[lm] = 0;
            pq.push({0, lm});

            while (!pq.empty()) {
                auto [d, u] = pq.top(); pq.pop();
                if (d > dist[u]) continue;
                for (const auto& e : g.bwd(u)) {
                    Weight nd = d + e.weight;
                    if (nd < dist[e.to]) {
                        dist[e.to] = nd;
                        pq.push({nd, e.to});
                    }
                }
            }
        }
    }

    if (verbose) std::cerr << "[ALT] Preprocessing complete.\n";
    return alt;
}

// ── ALT query (A* with ALT heuristic) ────────────────────────────────────────
RouteResult alt_query(const Graph& g, const ALTData& alt,
                      NodeId src, NodeId dst)
{
    const uint32_t N = g.num_nodes();

    struct E {
        Weight f, gv; NodeId n;
        bool operator>(const E& o) const noexcept { return f > o.f; }
    };
    std::priority_queue<E, std::vector<E>, std::greater<E>> pq;

    std::vector<Weight> cost(N, INF_WEIGHT);
    std::vector<NodeId> prev(N, INVALID_NODE);
    std::vector<NodeId> touched;

    auto heuristic = [&](NodeId u) -> Weight {
        // Max of ALT and Haversine/max_speed
        Weight h_alt = alt_heuristic(alt, u, dst);
        Weight h_geo = g.heuristic(u, dst);
        return std::max(h_alt, h_geo);
    };

    cost[src] = 0;
    touched.push_back(src);
    pq.push({heuristic(src), 0, src});

    while (!pq.empty()) {
        E top = pq.top(); pq.pop();
        Weight gv = top.gv;
        NodeId u  = top.n;
        if (gv > cost[u]) continue;
        if (u == dst) break;

        for (uint64_t ei = 0; ei < g.fwd(u).size(); ++ei) {
            const auto& edge = g.fwd(u)[ei];
            Weight ng = cost[u] + edge.weight;
            if (ng < cost[edge.to]) {
                if (cost[edge.to] == INF_WEIGHT) touched.push_back(edge.to);
                cost[edge.to] = ng;
                prev[edge.to] = u;
                pq.push({ng + heuristic(edge.to), ng, edge.to});
            }
        }
    }

    // Reset
    RouteResult r;
    if (cost[dst] == INF_WEIGHT) return r;
    r.cost  = cost[dst];
    r.found = true;
    for (NodeId v = dst; v != INVALID_NODE; v = prev[v])
        r.path.push_back(v);
    std::reverse(r.path.begin(), r.path.end());
    return r;
}

// ── Serialization ─────────────────────────────────────────────────────────────
static constexpr uint32_t ALT_MAGIC = 0xA17A17u;

void save_alt(const ALTData& alt, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write ALT file");
    uint32_t magic = ALT_MAGIC;
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&alt.num_nodes),     4);
    f.write(reinterpret_cast<const char*>(&alt.num_landmarks),  4);
    f.write(reinterpret_cast<const char*>(alt.landmarks.data()),
            alt.num_landmarks * sizeof(NodeId));

    for (uint32_t l = 0; l < alt.num_landmarks; ++l) {
        f.write(reinterpret_cast<const char*>(alt.dist_from_L[l].data()),
                alt.num_nodes * sizeof(Weight));
        f.write(reinterpret_cast<const char*>(alt.dist_to_L[l].data()),
                alt.num_nodes * sizeof(Weight));
    }
}

ALTData load_alt(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot read ALT file");
    uint32_t magic{};
    f.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != ALT_MAGIC) throw std::runtime_error("Bad ALT magic");

    ALTData alt;
    f.read(reinterpret_cast<char*>(&alt.num_nodes),     4);
    f.read(reinterpret_cast<char*>(&alt.num_landmarks),  4);
    alt.landmarks.resize(alt.num_landmarks);
    f.read(reinterpret_cast<char*>(alt.landmarks.data()),
           alt.num_landmarks * sizeof(NodeId));
    alt.dist_from_L.resize(alt.num_landmarks,
                            std::vector<Weight>(alt.num_nodes));
    alt.dist_to_L  .resize(alt.num_landmarks,
                            std::vector<Weight>(alt.num_nodes));
    for (uint32_t l = 0; l < alt.num_landmarks; ++l) {
        f.read(reinterpret_cast<char*>(alt.dist_from_L[l].data()),
               alt.num_nodes * sizeof(Weight));
        f.read(reinterpret_cast<char*>(alt.dist_to_L[l].data()),
               alt.num_nodes * sizeof(Weight));
    }
    return alt;
}

} // namespace routing
