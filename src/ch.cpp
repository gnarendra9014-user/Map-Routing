// ============================================================================
//  ch.cpp -- Contraction Hierarchies: preprocessing and query
// ============================================================================
#include "ch.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <unordered_set>
#include <vector>

namespace routing {

// ============================================================================
//  PREPROCESSING
// ============================================================================

// -- Working graph during contraction -----------------------------------------
// We need a mutable graph we can add shortcuts to and mark nodes contracted.
// We represent it as: for each node u, a list of live (src/dst) edges.

struct WorkEdge {
    NodeId  other;       // the other endpoint
    Weight  weight;
    NodeId  via;         // INVALID_NODE for originals, else shortcut via
    bool    is_out;      // true=outgoing from u, false=incoming
};

struct WorkGraph {
    uint32_t N;
    std::vector<std::vector<WorkEdge>> adj; // adj[u] = all live edges incident to u

    explicit WorkGraph(const Graph& g)
        : N(g.num_nodes())
        , adj(g.num_nodes())
    {
        // Populate from forward CSR
        for (NodeId u = 0; u < N; ++u) {
            for (const auto& e : g.fwd(u)) {
                adj[u].push_back({ e.to,   e.weight, INVALID_NODE, true  });
                adj[e.to].push_back({ u,   e.weight, INVALID_NODE, false });
            }
        }
    }

    // Outgoing edges from u (excluding contracted nodes)
    std::vector<WorkEdge> out_edges(NodeId u,
                                    const std::vector<bool>& contracted) const {
        std::vector<WorkEdge> res;
        for (const auto& e : adj[u])
            if (e.is_out && !contracted[e.other])
                res.push_back(e);
        return res;
    }

    // Incoming edges to u (excluding contracted nodes)
    std::vector<WorkEdge> in_edges(NodeId u,
                                   const std::vector<bool>& contracted) const {
        std::vector<WorkEdge> res;
        for (const auto& e : adj[u])
            if (!e.is_out && !contracted[e.other])
                res.push_back(e);
        return res;
    }

    void add_shortcut(NodeId from, NodeId to, Weight w, NodeId via) {
        adj[from].push_back({ to,   w, via, true  });
        adj[to  ].push_back({ from, w, via, false });
    }
};

// -- Local Dijkstra (witness search) ------------------------------------------
// Run a Dijkstra from `src` on the work graph (ignoring `skip`),
// limited to `limit` weight and `max_settled` settled nodes.
// Uses a flat vector for dist (fast), tracks touched nodes for cleanup.
static Weight local_dijkstra(
    const WorkGraph&          wg,
    const std::vector<bool>&  contracted,
    NodeId                    src,
    NodeId                    dst,
    NodeId                    skip,
    Weight                    limit,
    uint32_t                  max_settled,
    std::vector<Weight>&      dist_buf,   // reusable buffer, size N, init INF_WEIGHT
    std::vector<NodeId>&      touched_buf // reusable buffer for cleanup
)
{
    struct E { Weight d; NodeId n;
               bool operator>(const E& o) const noexcept { return d > o.d; } };

    std::priority_queue<E, std::vector<E>, std::greater<E>> pq;
    dist_buf[src] = 0;
    touched_buf.push_back(src);
    pq.push({0, src});

    uint32_t settled = 0;

    while (!pq.empty() && settled < max_settled) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist_buf[u]) continue;
        if (d > limit) break;
        if (u == dst) break;  // early termination: found target
        ++settled;

        for (const auto& e : wg.adj[u]) {
            if (!e.is_out) continue;
            if (e.other == skip || contracted[e.other]) continue;
            Weight nd = d + e.weight;
            if (nd < dist_buf[e.other]) {
                dist_buf[e.other] = nd;
                touched_buf.push_back(e.other);
                pq.push({nd, e.other});
            }
        }
    }

    Weight result = dist_buf[dst];

    // Cleanup touched nodes
    for (NodeId v : touched_buf)
        dist_buf[v] = INF_WEIGHT;
    touched_buf.clear();

    return result;
}

// -- Compute importance of node u ---------------------------------------------
static int32_t compute_importance(
    const WorkGraph&          wg,
    const std::vector<bool>&  contracted,
    const std::vector<CHNode>& ch_nodes,
    NodeId                    u,
    uint32_t                  max_settled,
    std::vector<Weight>&      dist_buf,
    std::vector<NodeId>&      touched_buf)
{
    const auto ins = wg.in_edges(u, contracted);
    const auto outs = wg.out_edges(u, contracted);

    // Simulate contraction: count shortcuts needed
    int32_t shortcuts = 0;
    int32_t edges_removed = static_cast<int32_t>(ins.size() + outs.size());

    for (const auto& in_e : ins) {
        for (const auto& out_e : outs) {
            if (in_e.other == out_e.other) continue;
            const Weight via_cost = in_e.weight + out_e.weight;
            // Witness search: can we reach out_e.other from in_e.other
            //                 without going through u, for <= via_cost?
            Weight w = local_dijkstra(wg, contracted,
                                      in_e.other, out_e.other,
                                      u, via_cost, max_settled,
                                      dist_buf, touched_buf);
            if (w > via_cost) ++shortcuts;
        }
    }

    int32_t edge_diff       = shortcuts - edges_removed;
    int32_t contracted_nbrs = ch_nodes[u].contracted_neighbors;
    int32_t depth           = static_cast<int32_t>(ch_nodes[u].depth);

    return edge_diff + contracted_nbrs + depth;
}

// -- Contract a single node ---------------------------------------------------
static void contract_node(
    WorkGraph&                wg,
    const std::vector<bool>&  contracted,
    std::vector<CHNode>&      ch_nodes,
    CHGraph&                  ch,
    NodeId                    u,
    uint32_t                  rank,
    uint32_t                  max_settled,
    std::vector<Weight>&      dist_buf,
    std::vector<NodeId>&      touched_buf)
{
    const auto ins  = wg.in_edges(u, contracted);
    const auto outs = wg.out_edges(u, contracted);

    uint32_t max_depth = ch_nodes[u].depth;

    for (const auto& in_e : ins) {
        for (const auto& out_e : outs) {
            if (in_e.other == out_e.other) continue;
            const Weight via_cost = in_e.weight + out_e.weight;

            Weight w = local_dijkstra(wg, contracted,
                                      in_e.other, out_e.other,
                                      u, via_cost, max_settled,
                                      dist_buf, touched_buf);
            if (w > via_cost) {
                // Insert shortcut
                wg.add_shortcut(in_e.other, out_e.other, via_cost, u);
                ++ch.num_shortcuts;

                // Record in CH upward graph
                // The shortcut goes in_e.other -> out_e.other
                ch.upward[in_e.other].push_back(
                    { out_e.other, via_cost, u, true });
                ch.downward[out_e.other].push_back(
                    { in_e.other, via_cost, u, false });

                // Track depth
                uint32_t sc_depth = std::max(ch_nodes[in_e.other].depth,
                                             ch_nodes[out_e.other].depth) + 1;
                max_depth = std::max(max_depth, sc_depth);

                ch_nodes[in_e.other].depth = std::max(
                    ch_nodes[in_e.other].depth, sc_depth);
                ch_nodes[out_e.other].depth = std::max(
                    ch_nodes[out_e.other].depth, sc_depth);
            }
        }
    }

    // Record original edges from u into the CH upward graph
    for (const auto& e : outs) {
        ch.upward[u].push_back({ e.other, e.weight, INVALID_NODE, true });
        ch.downward[e.other].push_back({ u, e.weight, INVALID_NODE, false });
    }
    for (const auto& e : ins) {
        ch.upward[e.other].push_back({ u, e.weight, INVALID_NODE, true });
        ch.downward[u].push_back({ e.other, e.weight, INVALID_NODE, false });
    }

    ch_nodes[u].rank  = rank;
    ch_nodes[u].depth = max_depth;

    // Update contracted_neighbors count for all live neighbors
    for (const auto& e : ins)
        ++ch_nodes[e.other].contracted_neighbors;
    for (const auto& e : outs)
        ++ch_nodes[e.other].contracted_neighbors;
}

// -- Main preprocessing -------------------------------------------------------
CHGraph build_ch(const Graph& g, bool verbose) {
    const uint32_t N = g.num_nodes();
    CHGraph ch;
    ch.num_nodes  = N;
    ch.base_graph = &g;
    ch.upward.resize(N);
    ch.downward.resize(N);
    ch.nodes.resize(N);

    WorkGraph wg(g);
    std::vector<bool> contracted(N, false);

    // Reusable buffers for local_dijkstra (avoid repeated allocation)
    std::vector<Weight> dist_buf(N, INF_WEIGHT);
    std::vector<NodeId> touched_buf;
    touched_buf.reserve(1024);

    // Witness search hop limit: keep small for speed
    constexpr uint32_t MAX_SETTLED = 50;

    // -- Initial priority queue ------------------------------------------------
    // importance is a signed value; PQ is a min-heap
    using PQE = std::pair<int32_t, NodeId>;
    std::priority_queue<PQE, std::vector<PQE>, std::greater<PQE>> pq;

    if (verbose) std::cerr << "[CH] Computing initial importance for " << N << " nodes...\n";
    for (NodeId u = 0; u < N; ++u) {
        int32_t imp = compute_importance(wg, contracted, ch.nodes, u,
                                         MAX_SETTLED, dist_buf, touched_buf);
        pq.push({imp, u});
    }

    uint32_t rank = 0;
    uint32_t progress_step = std::max(1u, N / 100);

    while (!pq.empty()) {
        auto [imp, u] = pq.top(); pq.pop();

        if (contracted[u]) continue;

        // Lazy update: recompute importance before contracting
        int32_t fresh_imp = compute_importance(wg, contracted, ch.nodes, u,
                                               MAX_SETTLED, dist_buf, touched_buf);
        if (fresh_imp > imp) {
            // Someone else became better; re-queue
            pq.push({fresh_imp, u});
            continue;
        }

        contracted[u] = true;
        contract_node(wg, contracted, ch.nodes, ch, u, rank,
                      MAX_SETTLED, dist_buf, touched_buf);
        ++rank;

        if (verbose && rank % progress_step == 0)
            std::cerr << "[CH] Contracted " << rank << "/" << N
                      << "  shortcuts=" << ch.num_shortcuts << "\r";
    }

    if (verbose)
        std::cerr << "\n[CH] Done. Rank=" << rank
                  << "  shortcuts=" << ch.num_shortcuts << "\n";

    return ch;
}

// ============================================================================
//  QUERY
// ============================================================================

// -- Bidirectional Dijkstra on upward graph ------------------------------------
RouteResult ch_query(const CHGraph& ch, NodeId src, NodeId dst) {
    const uint32_t N = ch.num_nodes;

    struct E {
        Weight d; NodeId n;
        bool operator>(const E& o) const noexcept { return d > o.d; }
    };

    std::vector<Weight> dist_f(N, INF_WEIGHT);
    std::vector<Weight> dist_b(N, INF_WEIGHT);
    std::vector<NodeId> prev_f(N, INVALID_NODE);
    std::vector<NodeId> prev_b(N, INVALID_NODE);

    std::priority_queue<E, std::vector<E>, std::greater<E>> pq_f, pq_b;

    dist_f[src] = 0; pq_f.push({0, src});
    dist_b[dst] = 0; pq_b.push({0, dst});

    Weight   mu      = INF_WEIGHT;
    NodeId   meeting = INVALID_NODE;

    auto relax_fwd = [&](NodeId u) {
        for (const auto& e : ch.upward[u]) {
            // Only expand upward (to higher-rank nodes)
            if (ch.nodes[e.to].rank <= ch.nodes[u].rank) continue;
            Weight nd = dist_f[u] + e.weight;
            if (nd < dist_f[e.to]) {
                dist_f[e.to] = nd;
                prev_f[e.to] = u;
                pq_f.push({nd, e.to});
                if (dist_b[e.to] != INF_WEIGHT && nd + dist_b[e.to] < mu) {
                    mu      = nd + dist_b[e.to];
                    meeting = e.to;
                }
            }
        }
    };

    auto relax_bwd = [&](NodeId u) {
        for (const auto& e : ch.downward[u]) {
            if (ch.nodes[e.to].rank <= ch.nodes[u].rank) continue;
            Weight nd = dist_b[u] + e.weight;
            if (nd < dist_b[e.to]) {
                dist_b[e.to] = nd;
                prev_b[e.to] = u;
                pq_b.push({nd, e.to});
                if (dist_f[e.to] != INF_WEIGHT && dist_f[e.to] + nd < mu) {
                    mu      = dist_f[e.to] + nd;
                    meeting = e.to;
                }
            }
        }
    };

    // Initial relaxation
    relax_fwd(src);
    relax_bwd(dst);

    std::vector<NodeId> visited;

    while (!pq_f.empty() || !pq_b.empty()) {
        // Termination: both heaps' tops >= mu
        Weight top_f = pq_f.empty() ? INF_WEIGHT : pq_f.top().d;
        Weight top_b = pq_b.empty() ? INF_WEIGHT : pq_b.top().d;
        if (top_f >= mu && top_b >= mu) break;

        if (top_f <= top_b && !pq_f.empty()) {
            auto [d, u] = pq_f.top(); pq_f.pop();
            if (d > dist_f[u]) continue;
            visited.push_back(u);
            relax_fwd(u);
        } else if (!pq_b.empty()) {
            auto [d, u] = pq_b.top(); pq_b.pop();
            if (d > dist_b[u]) continue;
            visited.push_back(u);
            relax_bwd(u);
        }
    }

    if (meeting == INVALID_NODE) return {};

    // -- Unpack path through meeting node -------------------------------------
    RouteResult r;
    r.cost  = mu;
    r.found = true;

    // Build raw node path (may contain shortcut jumps)
    std::vector<NodeId> fwd_path;
    for (NodeId v = meeting; v != INVALID_NODE; v = prev_f[v])
        fwd_path.push_back(v);
    std::reverse(fwd_path.begin(), fwd_path.end());

    std::vector<NodeId> bwd_path;
    for (NodeId v = prev_b[meeting]; v != INVALID_NODE; v = prev_b[v])
        bwd_path.push_back(v);

    r.path = std::move(fwd_path);
    r.path.insert(r.path.end(), bwd_path.begin(), bwd_path.end());
    r.visited = std::move(visited);

    return r;
}

// ============================================================================
//  SERIALIZATION
// ============================================================================
static constexpr uint32_t CH_MAGIC = 0xC0C0C001u;

void save_ch(const CHGraph& ch, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write CH file: " + path);

    uint32_t magic = 0xC0C0C001u;
    uint32_t N     = ch.num_nodes;
    uint64_t sc    = ch.num_shortcuts;
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&N),     4);
    f.write(reinterpret_cast<const char*>(&sc),    8);

    // Node data
    f.write(reinterpret_cast<const char*>(ch.nodes.data()), N * sizeof(CHNode));

    // Adjacency lists (upward + downward)
    auto write_adj = [&](const std::vector<std::vector<CHEdge>>& adj) {
        for (uint32_t u = 0; u < N; ++u) {
            uint32_t sz = static_cast<uint32_t>(adj[u].size());
            f.write(reinterpret_cast<const char*>(&sz), 4);
            f.write(reinterpret_cast<const char*>(adj[u].data()),
                    sz * sizeof(CHEdge));
        }
    };
    write_adj(ch.upward);
    write_adj(ch.downward);
}

CHGraph load_ch(const std::string& path, const Graph& base) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot read CH file: " + path);

    uint32_t magic{}, N{};
    uint64_t sc{};
    f.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != 0xC0C0C001u) throw std::runtime_error("Bad CH magic");
    f.read(reinterpret_cast<char*>(&N), 4);
    f.read(reinterpret_cast<char*>(&sc), 8);

    CHGraph ch;
    ch.num_nodes    = N;
    ch.num_shortcuts= sc;
    ch.base_graph   = &base;
    ch.nodes.resize(N);
    ch.upward.resize(N);
    ch.downward.resize(N);

    f.read(reinterpret_cast<char*>(ch.nodes.data()), N * sizeof(CHNode));

    auto read_adj = [&](std::vector<std::vector<CHEdge>>& adj) {
        for (uint32_t u = 0; u < N; ++u) {
            uint32_t sz{};
            f.read(reinterpret_cast<char*>(&sz), 4);
            adj[u].resize(sz);
            f.read(reinterpret_cast<char*>(adj[u].data()),
                   sz * sizeof(CHEdge));
        }
    };
    read_adj(ch.upward);
    read_adj(ch.downward);

    return ch;
}

} // namespace routing
