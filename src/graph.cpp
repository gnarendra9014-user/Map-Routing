// ============================================================================
//  graph.cpp — CSR graph construction and serialization
// ============================================================================
#include "graph.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace routing {

// ── Helper: raw edge used during construction ─────────────────────────────────
struct RawEdge {
    NodeId from{0};
    NodeId to{0};
    Weight weight{0};
    float  speed_kmh{50.0f};
};

// ── CSR builder ───────────────────────────────────────────────────────────────
//  Given a list of directed raw edges and node count N,
//  fills head[] and adj[] in CSR format.
static void build_csr(
    uint32_t              N,
    const std::vector<RawEdge>& edges,   // edges in any order
    bool                  reverse,       // if true, build bwd graph
    std::vector<EdgeId>&  head,
    std::vector<AdjEntry>& adj)
{
    head.assign(N + 1, 0);

    // Count degree of each source (or dest if reversed)
    for (const auto& e : edges) {
        NodeId src = reverse ? e.to : e.from;
        ++head[src + 1];
    }

    // Prefix-sum → start of each row
    std::partial_sum(head.begin(), head.end(), head.begin());

    // Fill adj[]
    adj.resize(edges.size());
    std::vector<EdgeId> cursor(head.begin(), head.begin() + N); // writable copy

    for (const auto& e : edges) {
        NodeId src = reverse ? e.to  : e.from;
        NodeId dst = reverse ? e.from : e.to;
        EdgeId pos = cursor[src]++;
        adj[pos] = { dst, e.weight, e.speed_kmh };
    }
}

// ── Graph::build ──────────────────────────────────────────────────────────────
Graph Graph::build(const OsmData& data) {
    Graph g;

    if (data.nodes.empty()) {
        std::cerr << "[Graph] Warning: no nodes\n";
        return g;
    }

    const uint32_t N = static_cast<uint32_t>(data.nodes.size());

    // Store coordinates (they are already compacted 0..N-1 by node_index)
    g.coords_.resize(N);
    for (const auto& n : data.nodes) {
        size_t idx = data.node_index.at(n.id);
        g.coords_[idx] = { n.lat, n.lon };
    }

    // Convert OsmEdge → RawEdge (OSM IDs → compact IDs, compute weights)
    std::vector<RawEdge> raw;
    raw.reserve(data.edges.size());

    double max_spd = 0.0;

    for (const auto& oe : data.edges) {
        auto it_f = data.node_index.find(oe.from);
        auto it_t = data.node_index.find(oe.to);
        if (it_f == data.node_index.end() || it_t == data.node_index.end())
            continue;

        NodeId from = static_cast<NodeId>(it_f->second);
        NodeId to   = static_cast<NodeId>(it_t->second);

        // Skip self-loops
        if (from == to) continue;

        const double dist_m = geo::haversine_m(g.coords_[from], g.coords_[to]);
        const Weight w      = geo::travel_time_cs(dist_m, oe.speed_kmh);

        // Skip zero-length edges (duplicate nodes at same coordinate)
        if (w == 0) continue;

        raw.push_back({ from, to, w, static_cast<float>(oe.speed_kmh) });
        max_spd = std::max(max_spd, oe.speed_kmh);
    }

    g.max_speed_kmh_ = (max_spd > 0.0) ? max_spd : 130.0;

    std::cerr << "[Graph] N=" << N
              << "  M=" << raw.size()
              << "  max_speed=" << g.max_speed_kmh_ << " km/h\n";

    // Build forward CSR
    build_csr(N, raw, false, g.fwd_head_, g.fwd_adj_);
    // Build backward CSR
    build_csr(N, raw, true,  g.bwd_head_, g.bwd_adj_);

    return g;
}

// ── Serialization ─────────────────────────────────────────────────────────────
// Binary format (little-endian):
//   [uint32 magic=0xCAFE0001]
//   [uint32 N] [uint64 M]
//   [double max_speed]
//   [N+1 EdgeId: fwd_head] [M AdjEntry: fwd_adj]
//   [N+1 EdgeId: bwd_head] [M AdjEntry: bwd_adj]
//   [N LatLon: coords]

static constexpr uint32_t MAGIC = 0xCAFE0001u;

template<typename T>
static void write_vec(std::ofstream& f, const std::vector<T>& v) {
    uint64_t sz = v.size();
    f.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    f.write(reinterpret_cast<const char*>(v.data()), sz * sizeof(T));
}

template<typename T>
static void read_vec(std::ifstream& f, std::vector<T>& v) {
    uint64_t sz{};
    f.read(reinterpret_cast<char*>(&sz), sizeof(sz));
    v.resize(sz);
    f.read(reinterpret_cast<char*>(v.data()), sz * sizeof(T));
}

void Graph::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open " + path + " for write");

    f.write(reinterpret_cast<const char*>(&MAGIC), sizeof(MAGIC));
    uint32_t N = num_nodes();
    f.write(reinterpret_cast<const char*>(&N), sizeof(N));
    f.write(reinterpret_cast<const char*>(&max_speed_kmh_), sizeof(max_speed_kmh_));
    write_vec(f, fwd_head_);
    write_vec(f, fwd_adj_);
    write_vec(f, bwd_head_);
    write_vec(f, bwd_adj_);
    write_vec(f, coords_);
}

Graph Graph::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open " + path + " for read");

    uint32_t magic{};
    f.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != MAGIC)
        throw std::runtime_error("Bad magic — not a routing graph file");

    Graph g;
    uint32_t N{};
    f.read(reinterpret_cast<char*>(&N), sizeof(N));
    f.read(reinterpret_cast<char*>(&g.max_speed_kmh_), sizeof(g.max_speed_kmh_));
    read_vec(f, g.fwd_head_);
    read_vec(f, g.fwd_adj_);
    read_vec(f, g.bwd_head_);
    read_vec(f, g.bwd_adj_);
    read_vec(f, g.coords_);
    return g;
}

} // namespace routing
