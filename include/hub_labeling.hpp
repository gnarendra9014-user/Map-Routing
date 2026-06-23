#pragma once
// ============================================================================
//  hub_labeling.hpp — Hub Labeling (exact, microsecond queries)
//
//  Construction (CH-based):
//  ─────────────────────────
//  Process nodes in decreasing CH rank order.
//  For each node v:
//    - Start with a forward label: {v, 0}
//    - For each upward edge v→w: merge w's forward label into v's, adding w.weight
//    - Prune: keep hub h in L_f(v) only if ch_query(v,h) == dist_f[v][h]
//             (no existing hub already covers this path)
//
//  Query:
//  ──────
//  L_f(s) ∩ L_b(t) by iterating both sorted label vectors simultaneously.
//  Result = min over common hubs h of L_f(s)[h] + L_b(t)[h].
//
//  Memory: O(N log N) labels on road graphs (~100-200 labels/node avg).
// ============================================================================
#include "types.hpp"
#include "ch.hpp"
#include <string>
#include <vector>

namespace routing {

// A single hub entry in a label
struct HubEntry {
    NodeId hub;
    Weight dist;
};

struct HubLabels {
    uint32_t num_nodes{0};
    // Forward labels: L_f[v] = list of (hub, dist_from_v_to_hub)
    std::vector<std::vector<HubEntry>> L_f;
    // Backward labels: L_b[v] = list of (hub, dist_from_hub_to_v)
    std::vector<std::vector<HubEntry>> L_b;
};

// ── Preprocessing ─────────────────────────────────────────────────────────────
HubLabels build_hub_labels(const CHGraph& ch);

// ── Query (label intersection) ────────────────────────────────────────────────
// O(|L_f(s)| + |L_b(t)|) — typically ~200-400 comparisons
Weight hub_query(const HubLabels& hl, NodeId src, NodeId dst);

// Full route result (hub labels only give cost, not path — path needs CH)
RouteResult hub_query_full(const HubLabels& hl, const CHGraph& ch,
                           NodeId src, NodeId dst);

// ── Serialization ─────────────────────────────────────────────────────────────
void save_hub_labels(const HubLabels& hl, const std::string& path);
HubLabels load_hub_labels(const std::string& path);

} // namespace routing
