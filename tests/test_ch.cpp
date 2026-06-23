// ============================================================================
//  test_ch.cpp — Contraction Hierarchies correctness tests
//
//  We verify:
//  1. CH gives same cost as Dijkstra on all pairs of a small graph
//  2. Hub label query gives same cost as CH
//  3. Shortcut count is sane
// ============================================================================
#include "osm_parser.hpp"
#include "graph.hpp"
#include "dijkstra.hpp"
#include "ch.hpp"
#include "hub_labeling.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace routing;

// ── Build test graph ──────────────────────────────────────────────────────────
// 6-node graph with a mix of direct and longer routes
static OsmData make_ch_test_data() {
    OsmData d;
    // Place nodes 0.01° apart (~1.1km each)
    for (int i = 0; i < 6; ++i) {
        d.nodes.push_back({ (int64_t)i, (double)i * 0.01, 0.0 });
        d.node_index[(int64_t)i] = (size_t)i;
    }
    // Edges (bidirectional)
    auto add = [&](int a, int b, double spd) {
        d.edges.push_back({ (int64_t)a, (int64_t)b, spd, false });
        d.edges.push_back({ (int64_t)b, (int64_t)a, spd, false });
    };
    add(0, 1, 50.0);
    add(1, 2, 50.0);
    add(2, 3, 80.0);  // fast segment
    add(3, 4, 50.0);
    add(4, 5, 50.0);
    add(0, 3, 30.0);  // slow direct path
    add(1, 4, 60.0);  // shortcut-ish
    return d;
}

// ── Enumerate all pairs ───────────────────────────────────────────────────────
void test_ch_correctness() {
    auto d = make_ch_test_data();
    Graph g = Graph::build(d);

    Dijkstra dijk(g);
    CHGraph  ch = build_ch(g, false /*verbose=false*/);

    const uint32_t N = g.num_nodes();
    uint32_t checked = 0;

    for (NodeId src = 0; src < N; ++src) {
        for (NodeId dst = 0; dst < N; ++dst) {
            auto r_d = dijk.query(src, dst);
            auto r_c = ch_query(ch, src, dst);

            assert(r_d.found == r_c.found && "found must agree");
            if (r_d.found) {
                assert(r_d.cost == r_c.cost
                       && "CH cost must match Dijkstra");
            }
            ++checked;
        }
    }
    std::cout << "  [PASS] CH == Dijkstra on " << checked << " pairs\n";
}

// ── Test: shortcuts are non-negative count ─────────────────────────────────────
void test_ch_shortcuts() {
    auto d = make_ch_test_data();
    Graph g = Graph::build(d);
    CHGraph ch = build_ch(g, false);
    // Shortcuts should exist but not be astronomically many
    assert(ch.num_shortcuts < 1000 && "Shortcut count sanity check");
    std::cout << "  [PASS] CH shortcuts = " << ch.num_shortcuts << "\n";
}

// ── Test: CH serialization round-trip ────────────────────────────────────────
void test_ch_serialization() {
    auto d = make_ch_test_data();
    Graph g = Graph::build(d);
    CHGraph ch = build_ch(g, false);
    save_ch(ch, "test_ch_tmp.bin");
    CHGraph ch2 = load_ch("test_ch_tmp.bin", g);

    assert(ch.num_nodes == ch2.num_nodes);
    assert(ch.num_shortcuts == ch2.num_shortcuts);

    // Verify query still works after reload
    auto r1 = ch_query(ch,  0, 5);
    auto r2 = ch_query(ch2, 0, 5);
    assert(r1.found == r2.found);
    if (r1.found) assert(r1.cost == r2.cost);
    std::cout << "  [PASS] CH serialization round-trip\n";
}

// ── Test: Hub labels give same cost as CH ─────────────────────────────────────
void test_hub_labels_correctness() {
    auto d = make_ch_test_data();
    Graph    g  = Graph::build(d);
    CHGraph  ch = build_ch(g, false);
    HubLabels hl = build_hub_labels(ch);

    const uint32_t N = g.num_nodes();
    for (NodeId src = 0; src < N; ++src) {
        for (NodeId dst = 0; dst < N; ++dst) {
            Weight w_ch = ch_query(ch, src, dst).cost;
            Weight w_hl = hub_query(hl, src, dst);

            bool reachable_ch = (w_ch != INF_WEIGHT);
            bool reachable_hl = (w_hl != INF_WEIGHT);
            assert(reachable_ch == reachable_hl && "HL reachability must match CH");
            if (reachable_ch) {
                assert(w_ch == w_hl && "HL cost must match CH cost");
            }
        }
    }
    std::cout << "  [PASS] HubLabels == CH on all " << N*N << " pairs\n";
}

int main() {
    std::cout << "=== test_ch ===\n";
    test_ch_correctness();
    test_ch_shortcuts();
    test_ch_serialization();
    test_hub_labels_correctness();
    std::cout << "All CH tests passed.\n";
    return 0;
}
