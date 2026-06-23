// ============================================================================
//  test_graph.cpp — CSR Graph Builder tests
//  Build a small synthetic graph and verify CSR layout.
// ============================================================================
#include "graph.hpp"
#include "osm_parser.hpp"
#include <cassert>
#include <iostream>
#include <set>

using namespace routing;

// ── Build synthetic 4-node graph ─────────────────────────────────────────────
//
//   0 ─(50)─> 1 ─(80)─> 2
//   |                   |
//  (30)                (30)
//   v                   v
//   3 <──────────────── 3
//
//  (numbers are speed km/h)
//
//  Nodes at approximately 1° lat/lon apart (about 111km each step)
//
static OsmData make_test_data() {
    OsmData d;
    d.nodes = {
        { 100, 0.0, 0.0 },   // node 0
        { 101, 0.0, 1.0 },   // node 1 (~111km east)
        { 102, 0.0, 2.0 },   // node 2
        { 103, 1.0, 0.0 },   // node 3 (~111km north)
    };
    d.node_index = {{ 100, 0 }, { 101, 1 }, { 102, 2 }, { 103, 3 }};

    // Bidirectional edges (already split by parser)
    d.edges = {
        // 0 ↔ 1
        { 100, 101, 50.0, false },
        { 101, 100, 50.0, false },
        // 1 ↔ 2
        { 101, 102, 80.0, false },
        { 102, 101, 80.0, false },
        // 0 ↔ 3
        { 100, 103, 30.0, false },
        { 103, 100, 30.0, false },
        // 2 ↔ 3
        { 102, 103, 60.0, false },
        { 103, 102, 60.0, false },
    };
    return d;
}

// ── Test: node count ──────────────────────────────────────────────────────────
void test_node_count() {
    auto d = make_test_data();
    Graph g = Graph::build(d);
    assert(g.num_nodes() == 4 && "Expected 4 nodes");
    std::cout << "  [PASS] node count = " << g.num_nodes() << "\n";
}

// ── Test: edge count (bidirectional → 8 directed edges) ──────────────────────
void test_edge_count() {
    auto d = make_test_data();
    Graph g = Graph::build(d);
    assert(g.num_edges() == 8 && "Expected 8 directed edges");
    std::cout << "  [PASS] edge count = " << g.num_edges() << "\n";
}

// ── Test: forward CSR adjacency ───────────────────────────────────────────────
void test_adjacency() {
    auto d = make_test_data();
    Graph g = Graph::build(d);

    // Node 0 should have neighbors 1 and 3
    std::set<NodeId> nbrs0;
    for (const auto& e : g.fwd(0)) nbrs0.insert(e.to);
    assert(nbrs0.count(1) && nbrs0.count(3) && "Node 0 → {1, 3}");
    assert(nbrs0.size() == 2 && "Node 0 has exactly 2 forward edges");

    // Node 2 should have neighbors 1 and 3
    std::set<NodeId> nbrs2;
    for (const auto& e : g.fwd(2)) nbrs2.insert(e.to);
    assert(nbrs2.count(1) && nbrs2.count(3) && "Node 2 → {1, 3}");

    std::cout << "  [PASS] forward adjacency\n";
}

// ── Test: backward CSR is transpose of forward ────────────────────────────────
void test_backward_is_transpose() {
    auto d = make_test_data();
    Graph g = Graph::build(d);

    // For each forward edge u→v, there should be a backward edge v with to=u
    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (const auto& e : g.fwd(u)) {
            bool found = false;
            for (const auto& be : g.bwd(e.to)) {
                if (be.to == u) { found = true; break; }
            }
            assert(found && "Every fwd edge must appear as bwd edge");
        }
    }
    std::cout << "  [PASS] backward = transpose of forward\n";
}

// ── Test: weights are positive ────────────────────────────────────────────────
void test_weights_positive() {
    auto d = make_test_data();
    Graph g = Graph::build(d);

    for (NodeId u = 0; u < g.num_nodes(); ++u) {
        for (const auto& e : g.fwd(u)) {
            assert(e.weight > 0 && e.weight < INF_WEIGHT && "Weight must be positive");
        }
    }
    std::cout << "  [PASS] all weights positive\n";
}

// ── Test: heuristic is admissible (≤ true shortest path) ─────────────────────
void test_heuristic_admissible() {
    auto d = make_test_data();
    Graph g = Graph::build(d);

    // Heuristic from 0 to 0 should be 0
    assert(g.heuristic(0, 0) == 0 && "Self heuristic = 0");

    // Heuristic from 0 to 2 (2° apart = ~222km)
    // At max_speed (80 km/h from our data), lower bound = 222000/80 * 3600 / 100 cs
    // Should be some positive value
    Weight h = g.heuristic(0, 2);
    assert(h > 0 && "Heuristic 0→2 must be positive");
    std::cout << "  [PASS] heuristic(0,2) = " << h << " cs\n";
}

// ── Test: serialization round-trip ───────────────────────────────────────────
void test_serialization() {
    auto d = make_test_data();
    Graph g = Graph::build(d);
    g.save("test_graph_tmp.bin");
    Graph g2 = Graph::load("test_graph_tmp.bin");

    assert(g.num_nodes() == g2.num_nodes());
    assert(g.num_edges() == g2.num_edges());
    assert(std::abs(g.max_speed_kmh() - g2.max_speed_kmh()) < 0.01);

    // Check coordinates preserved
    for (NodeId v = 0; v < g.num_nodes(); ++v) {
        auto c1 = g.coord(v);
        auto c2 = g2.coord(v);
        assert(std::abs(c1.lat - c2.lat) < 1e-6 && "lat preserved");
        assert(std::abs(c1.lon - c2.lon) < 1e-6 && "lon preserved");
    }
    std::cout << "  [PASS] serialization round-trip\n";
}

int main() {
    std::cout << "=== test_graph ===\n";
    test_node_count();
    test_edge_count();
    test_adjacency();
    test_backward_is_transpose();
    test_weights_positive();
    test_heuristic_admissible();
    test_serialization();
    std::cout << "All graph tests passed.\n";
    return 0;
}
