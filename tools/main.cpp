// ============================================================================
//  main.cpp -- Map Routing Engine CLI + HTTP Server
//
//  Usage:
//    map_routing parse   <input.osm.pbf> <output.graph>
//    map_routing build   <input.graph> <output.ch> [--alt <output.alt>]
//    map_routing route   <graph> [--ch <ch>] [--alt <alt>] [--hl <hl>]
//                        --src <node_id>  --dst <node_id>
//                        [--algo dijkstra|astar|bidir|ch|alt|hl]
//                        [--out <output.geojson>]
//    map_routing bench   <graph> [--ch <ch>] --pairs <N>
//                        [--algo dijkstra|astar|bidir|ch|alt|hl]
//    map_routing info    <graph>
//    map_routing server  <graph> [--ch <ch>] [--port <8080>] [--web <dir>]
// ============================================================================

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#define CLOSESOCK closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

#include "osm_parser.hpp"
#include "graph.hpp"
#include "dijkstra.hpp"
#include "astar.hpp"
#include "bidirectional_astar.hpp"
#include "ch.hpp"
#include "alt.hpp"
#include "hub_labeling.hpp"
#include "geojson.hpp"
#include "types.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace routing;
using Clock = std::chrono::high_resolution_clock;
using Ms    = std::chrono::duration<double, std::milli>;

// -- Argument parser ----------------------------------------------------------
static std::string get_arg(const std::vector<std::string>& args,
                            std::string_view flag,
                            std::string_view default_val = "")
{
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == flag) return args[i + 1];
    return std::string(default_val);
}

static bool has_flag(const std::vector<std::string>& args, std::string_view flag) {
    return std::find(args.begin(), args.end(), flag) != args.end();
}

// -- Timer helper -------------------------------------------------------------
struct Timer {
    std::chrono::time_point<Clock> t0 = Clock::now();
    double elapsed_ms() const {
        return Ms(Clock::now() - t0).count();
    }
    void report(std::string_view label) const {
        std::cerr << "[" << label << "] " << elapsed_ms() << " ms\n";
    }
};

// -- Benchmark helper ---------------------------------------------------------
static void print_stats(const std::vector<double>& times) {
    if (times.empty()) return;
    std::vector<double> sorted = times;
    std::sort(sorted.begin(), sorted.end());
    double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    std::cerr << "  Queries : " << times.size() << "\n"
              << "  Mean    : " << sum / times.size() << " ms\n"
              << "  p50     : " << sorted[sorted.size() * 50 / 100] << " ms\n"
              << "  p95     : " << sorted[sorted.size() * 95 / 100] << " ms\n"
              << "  p99     : " << sorted[std::min(sorted.size() - 1,
                                                   sorted.size() * 99 / 100)] << " ms\n"
              << "  Max     : " << sorted.back() << " ms\n";
}

// ============================================================================
//  SUBCOMMANDS
// ============================================================================

// -- parse --------------------------------------------------------------------
static int cmd_parse(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: parse <input.osm.pbf> <output.graph>\n";
        return 1;
    }
    const std::string pbf_path   = args[0];
    const std::string graph_path = args[1];

    std::cerr << "=== PARSE ===\n";
    OsmData osm;
    Timer t;
    size_t edges = parse_osm_pbf(pbf_path, osm);
    t.report("OSM parse");

    Graph g = Graph::build(osm);
    t.report("Graph build");

    g.save(graph_path);
    std::cerr << "Graph saved to " << graph_path << "\n";
    std::cerr << "Nodes: " << g.num_nodes() << "  Edges: " << g.num_edges() << "\n";
    return 0;
}

// -- build (CH + optional ALT/HL) ---------------------------------------------
static int cmd_build(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: build <graph> <ch_out> [--alt <alt_out>] [--hl <hl_out>]\n";
        return 1;
    }
    const std::string graph_path = args[0];
    const std::string ch_path    = args.size() > 1 ? args[1] : "output.ch";
    const std::string alt_path   = get_arg(args, "--alt");
    const std::string hl_path    = get_arg(args, "--hl");

    std::cerr << "=== BUILD CH ===\n";
    Timer t;
    Graph g = Graph::load(graph_path);
    t.report("Graph load");

    CHGraph ch = build_ch(g);
    t.report("CH build");
    save_ch(ch, ch_path);
    std::cerr << "CH saved to " << ch_path << "\n";

    if (!alt_path.empty()) {
        std::cerr << "=== BUILD ALT ===\n";
        Timer ta;
        ALTData alt = build_alt(g);
        ta.report("ALT build");
        save_alt(alt, alt_path);
        std::cerr << "ALT saved to " << alt_path << "\n";
    }

    if (!hl_path.empty()) {
        std::cerr << "=== BUILD HL ===\n";
        Timer thl;
        HubLabels hl = build_hub_labels(ch);
        thl.report("HL build");
        save_hub_labels(hl, hl_path);
        std::cerr << "HL saved to " << hl_path << "\n";
    }

    return 0;
}

// -- route --------------------------------------------------------------------
static int cmd_route(const std::vector<std::string>& args_all) {
    if (args_all.empty()) {
        std::cerr << "Usage: route <graph> [--ch <ch>] [--alt <alt>] "
                     "--src <id> --dst <id> [--algo ...] [--out <file>]\n";
        return 1;
    }
    const std::string graph_path = args_all[0];
    const std::string ch_path    = get_arg(args_all, "--ch");
    const std::string alt_path   = get_arg(args_all, "--alt");
    const std::string hl_path    = get_arg(args_all, "--hl");
    const std::string src_str    = get_arg(args_all, "--src");
    const std::string dst_str    = get_arg(args_all, "--dst");
    const std::string algo       = get_arg(args_all, "--algo", "astar");
    const std::string out_path   = get_arg(args_all, "--out");

    if (src_str.empty() || dst_str.empty()) {
        std::cerr << "Error: --src and --dst required\n";
        return 1;
    }

    NodeId src = static_cast<NodeId>(std::stoul(src_str));
    NodeId dst = static_cast<NodeId>(std::stoul(dst_str));

    Graph g = Graph::load(graph_path);
    std::cerr << "Graph: " << g.num_nodes() << " nodes, " << g.num_edges() << " edges\n";

    RouteResult result;
    Timer t;

    if (algo == "dijkstra") {
        Dijkstra dijk(g);
        result = dijk.query(src, dst);
    } else if (algo == "astar") {
        AStar astar(g);
        result = astar.query(src, dst);
        std::cerr << "Nodes expanded: " << astar.nodes_expanded() << "\n";
    } else if (algo == "bidir") {
        BidirectionalAStar bi(g);
        result = bi.query(src, dst);
        std::cerr << "Fwd settled: " << bi.fwd_settled()
                  << "  Bwd settled: " << bi.bwd_settled() << "\n";
    } else if (algo == "ch") {
        if (ch_path.empty()) { std::cerr << "Error: --ch required for ch algo\n"; return 1; }
        CHGraph ch = load_ch(ch_path, g);
        result = ch_query(ch, src, dst);
    } else if (algo == "alt") {
        if (alt_path.empty()) { std::cerr << "Error: --alt required for alt algo\n"; return 1; }
        ALTData alt = load_alt(alt_path);
        result = alt_query(g, alt, src, dst);
    } else if (algo == "hl") {
        if (hl_path.empty() || ch_path.empty()) {
            std::cerr << "Error: --hl and --ch required for hl algo\n"; return 1;
        }
        HubLabels hl   = load_hub_labels(hl_path);
        CHGraph   ch   = load_ch(ch_path, g);
        result = hub_query_full(hl, ch, src, dst);
    } else {
        std::cerr << "Unknown algorithm: " << algo << "\n";
        return 1;
    }

    t.report("Query");

    if (!result.found) {
        std::cerr << "No path found from " << src << " to " << dst << "\n";
        return 0;
    }

    const double dur_min = result.cost / 100.0 / 60.0;
    std::cerr << "Path found: " << result.path.size() << " nodes, "
              << dur_min << " min\n";

    export_geojson(g, result, out_path, algo + "_" + src_str + "_" + dst_str);
    return 0;
}

// -- bench --------------------------------------------------------------------
static int cmd_bench(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: bench <graph> [--ch <ch>] --pairs <N> --algo <algo>\n";
        return 1;
    }
    const std::string graph_path = args[0];
    const std::string ch_path    = get_arg(args, "--ch");
    const std::string alt_path   = get_arg(args, "--alt");
    const std::string algo       = get_arg(args, "--algo", "astar");
    const uint32_t    N_pairs    = static_cast<uint32_t>(
                                       std::stoul(get_arg(args, "--pairs", "1000")));

    Graph g = Graph::load(graph_path);
    std::cerr << "Graph: " << g.num_nodes() << " nodes\n";

    // Generate random pairs (seed 42 for reproducibility)
    std::mt19937 rng{42};
    std::uniform_int_distribution<NodeId> dist{0, g.num_nodes() - 1};
    std::vector<std::pair<NodeId,NodeId>> pairs(N_pairs);
    for (auto& [s, d] : pairs) { s = dist(rng); d = dist(rng); }

    // Load CH/ALT if needed
    std::unique_ptr<CHGraph>   ch;
    std::unique_ptr<ALTData>   alt;

    if (!ch_path.empty()) {
        ch = std::make_unique<CHGraph>(load_ch(ch_path, g));
    }
    if (!alt_path.empty()) {
        alt = std::make_unique<ALTData>(load_alt(alt_path));
    }

    std::vector<double> times;
    times.reserve(N_pairs);
    uint32_t found = 0;

    std::cerr << "=== BENCHMARK: " << algo << " x" << N_pairs << " ===\n";

    for (auto [src, dst] : pairs) {
        Timer t;
        RouteResult r;

        if (algo == "dijkstra") {
            Dijkstra dijk(g);
            r = dijk.query(src, dst);
        } else if (algo == "astar") {
            AStar astar(g);
            r = astar.query(src, dst);
        } else if (algo == "bidir") {
            BidirectionalAStar bi(g);
            r = bi.query(src, dst);
        } else if (algo == "ch" && ch) {
            r = ch_query(*ch, src, dst);
        } else if (algo == "alt" && alt) {
            r = alt_query(g, *alt, src, dst);
        }

        times.push_back(t.elapsed_ms());
        if (r.found) ++found;
    }

    std::cerr << "Found paths: " << found << "/" << N_pairs << "\n";
    print_stats(times);
    return 0;
}

// -- info ---------------------------------------------------------------------
static int cmd_info(const std::vector<std::string>& args) {
    if (args.empty()) { std::cerr << "Usage: info <graph>\n"; return 1; }
    Graph g = Graph::load(args[0]);
    std::cout << "Nodes     : " << g.num_nodes()    << "\n"
              << "Edges     : " << g.num_edges()    << "\n"
              << "Max speed : " << g.max_speed_kmh() << " km/h\n";
    // Degree distribution sample
    uint32_t N = g.num_nodes();
    uint64_t deg_sum = 0;
    uint32_t max_deg = 0;
    for (NodeId v = 0; v < N; ++v) {
        uint32_t d = static_cast<uint32_t>(g.fwd(v).size());
        deg_sum += d;
        max_deg = std::max(max_deg, d);
    }
    std::cout << "Avg degree: " << static_cast<double>(deg_sum) / N << "\n"
              << "Max degree: " << max_deg << "\n";
    return 0;
}

// ============================================================================
//  HTTP SERVER (embedded, no dependencies)
// ============================================================================

// Find nearest node to a given lat/lon
static NodeId find_nearest(const Graph& g, double lat, double lon) {
    NodeId best = 0;
    double best_dist = 1e18;
    uint32_t N = g.num_nodes();
    for (NodeId v = 0; v < N; ++v) {
        LatLon c = g.coord(v);
        double d = geo::haversine_m(lat, lon, c.lat, c.lon);
        if (d < best_dist) { best_dist = d; best = v; }
    }
    return best;
}

// Export all graph nodes as JSON (for visualization)
static std::string graph_bounds_json(const Graph& g) {
    double minLat = 90, maxLat = -90, minLon = 180, maxLon = -180;
    uint32_t N = g.num_nodes();
    for (NodeId v = 0; v < N; ++v) {
        LatLon c = g.coord(v);
        if (c.lat < minLat) minLat = c.lat;
        if (c.lat > maxLat) maxLat = c.lat;
        if (c.lon < minLon) minLon = c.lon;
        if (c.lon > maxLon) maxLon = c.lon;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(7);
    ss << "{\"nodes\":" << N << ",\"edges\":" << g.num_edges()
       << ",\"bounds\":{\"minLat\":" << minLat << ",\"maxLat\":" << maxLat
       << ",\"minLon\":" << minLon << ",\"maxLon\":" << maxLon << "}}";
    return ss.str();
}

static void write_route_fields(std::ostream& os, const Graph& g, const RouteResult& result, double query_ms) {
    os << std::fixed << std::setprecision(7);
    os << "\"found\":" << (result.found ? "true" : "false")
       << ",\"query_ms\":" << std::setprecision(3) << query_ms;

    if (result.found) {
        double dist_m = 0;
        double dur_s = result.cost / 100.0;
        os << ",\"nodes\":" << result.path.size()
           << ",\"duration_s\":" << std::setprecision(1) << dur_s
           << ",\"duration_min\":" << std::setprecision(2) << (dur_s / 60.0);

        // Coordinates array
        os << ",\"coordinates\":[";
        for (size_t i = 0; i < result.path.size(); ++i) {
            LatLon c = g.coord(result.path[i]);
            if (i > 0) {
                LatLon prev = g.coord(result.path[i-1]);
                dist_m += geo::haversine_m(prev, c);
                os << ",";
            }
            os << std::setprecision(7) << "[" << c.lat << "," << c.lon << "]";
        }
        os << "]";
        os << ",\"distance_m\":" << std::setprecision(1) << dist_m;

        // Visited coordinates array
        os << ",\"visited\":[";
        for (size_t i = 0; i < result.visited.size(); ++i) {
            LatLon c = g.coord(result.visited[i]);
            if (i > 0) os << ",";
            os << std::setprecision(7) << "[" << c.lat << "," << c.lon << "]";
        }
        os << "]";
    }
}

// Route and return JSON
struct AvoidCircle {
    double lat;
    double lon;
    double radius;
};

static std::vector<AvoidCircle> parse_avoidance_zones(const std::string& s) {
    std::vector<AvoidCircle> zones;
    if (s.empty()) return zones;
    size_t pos = 0;
    while (true) {
        pos = s.find('{', pos);
        if (pos == std::string::npos) break;
        size_t end_pos = s.find('}', pos);
        if (end_pos == std::string::npos) break;
        
        std::string obj = s.substr(pos, end_pos - pos + 1);
        
        auto extract_val = [](const std::string& obj_str, const std::string& key) -> double {
            auto key_pos = obj_str.find("\"" + key + "\"");
            if (key_pos == std::string::npos) {
                key_pos = obj_str.find(key);
                if (key_pos == std::string::npos) return 0.0;
            }
            auto colon_pos = obj_str.find(':', key_pos);
            if (colon_pos == std::string::npos) return 0.0;
            
            size_t val_pos = colon_pos + 1;
            while (val_pos < obj_str.size() && (obj_str[val_pos] == ' ' || obj_str[val_pos] == '\t')) {
                val_pos++;
            }
            
            try {
                size_t idx = 0;
                return std::stod(obj_str.substr(val_pos), &idx);
            } catch (...) {
                return 0.0;
            }
        };
        
        double lat = extract_val(obj, "lat");
        double lon = extract_val(obj, "lon");
        double radius = extract_val(obj, "radius");
        if (radius <= 0.0) {
            radius = extract_val(obj, "radius_m");
        }
        
        if (lat != 0.0 && lon != 0.0 && radius > 0.0) {
            zones.push_back({lat, lon, radius});
        }
        pos = end_pos + 1;
    }
    return zones;
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

// Route and return JSON
static std::string route_json(const Graph& g, const CHGraph* ch,
                               const ALTData* alt, const HubLabels* hl,
                               double srcLat, double srcLon,
                               double dstLat, double dstLon,
                               const std::string& algo,
                               bool alternatives = false,
                               int time_of_day = -1,
                               const std::string& profile = "fastest",
                               const std::string& avoidance_zones_str = "")
{
    Timer t;
    NodeId src = find_nearest(g, srcLat, srcLon);
    NodeId dst = find_nearest(g, dstLat, dstLon);
    double nearest_ms = t.elapsed_ms();

    // Parse avoidance zones
    std::vector<bool> node_avoided;
    auto avoid_circles = parse_avoidance_zones(avoidance_zones_str);
    if (!avoid_circles.empty()) {
        node_avoided.assign(g.num_nodes(), false);
        for (NodeId v = 0; v < g.num_nodes(); ++v) {
            LatLon coord = g.coord(v);
            for (const auto& circle : avoid_circles) {
                if (geo::haversine_m(coord.lat, coord.lon, circle.lat, circle.lon) <= circle.radius) {
                    node_avoided[v] = true;
                    break;
                }
            }
        }
    }

    bool has_dynamic = (time_of_day >= 0) || (profile != "fastest" && !profile.empty()) || (!node_avoided.empty());
    bool fallback_triggered = false;
    std::string active_algo = algo;
    if (has_dynamic && (algo == "ch" || algo == "hl" || algo == "alt" || algo == "bidir")) {
        fallback_triggered = true;
        active_algo = "astar";
    }

    RoutingOptions opts;
    opts.edge_penalties = {};
    opts.node_avoided = node_avoided;
    opts.time_of_day = time_of_day;
    opts.profile = profile;

    RouteResult result;
    Timer t2;

    if (active_algo == "dijkstra") {
        Dijkstra dijk(g);
        result = dijk.query(src, dst, opts);
    } else if (active_algo == "astar") {
        AStar astar(g);
        result = astar.query(src, dst, opts);
    } else if (active_algo == "bidir") {
        BidirectionalAStar bi(g);
        result = bi.query(src, dst);
    } else if (active_algo == "ch" && ch) {
        result = ch_query(*ch, src, dst);
    } else if (active_algo == "alt" && alt) {
        result = alt_query(g, *alt, src, dst);
    } else if (active_algo == "hl" && hl && ch) {
        result = hub_query_full(*hl, *ch, src, dst);
    } else {
        Dijkstra dijk(g);
        result = dijk.query(src, dst, opts);
    }

    double query_ms = t2.elapsed_ms();

    // Alternatives using Penalty Method
    std::vector<RouteResult> alt_results;
    std::vector<double> alt_query_times;
    if (alternatives && result.found && result.path.size() >= 3) {
        std::unordered_map<uint64_t, double> edge_penalties;
        
        auto penalize_path = [&](const std::vector<NodeId>& path, double factor) {
            for (size_t i = 0; i + 1 < path.size(); ++i) {
                NodeId u = path[i];
                NodeId v = path[i+1];
                edge_penalties[(static_cast<uint64_t>(u) << 32) | v] = factor;
            }
        };

        auto compute_overlap_pct = [&](const std::vector<NodeId>& p1, const std::vector<NodeId>& p2) {
            if (p1.empty() || p2.empty()) return 0.0;
            std::unordered_set<NodeId> s1(p1.begin(), p1.end());
            size_t shared = 0;
            for (NodeId node : p2) {
                if (s1.count(node)) ++shared;
            }
            return static_cast<double>(shared) / std::min(p1.size(), p2.size());
        };

        penalize_path(result.path, 1.6);
        
        AStar astar_engine(g);
        
        // Alt 1
        Timer t_alt1;
        RoutingOptions alt_opts = opts;
        alt_opts.edge_penalties = edge_penalties;
        RouteResult r_alt1 = astar_engine.query(src, dst, alt_opts);
        double q_alt1 = t_alt1.elapsed_ms();
        if (r_alt1.found && compute_overlap_pct(result.path, r_alt1.path) < 0.70) {
            alt_results.push_back(r_alt1);
            alt_query_times.push_back(q_alt1);
            penalize_path(r_alt1.path, 1.6);
            
            // Alt 2
            Timer t_alt2;
            alt_opts.edge_penalties = edge_penalties;
            RouteResult r_alt2 = astar_engine.query(src, dst, alt_opts);
            double q_alt2 = t_alt2.elapsed_ms();
            if (r_alt2.found && compute_overlap_pct(result.path, r_alt2.path) < 0.70 &&
                compute_overlap_pct(r_alt1.path, r_alt2.path) < 0.70) {
                alt_results.push_back(r_alt2);
                alt_query_times.push_back(q_alt2);
            }
        }
    }

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(7);
    ss << "{";
    write_route_fields(ss, g, result, query_ms);
    ss << ",\"algo\":\"" << algo << "\""
       << ",\"active_algo\":\"" << active_algo << "\""
       << ",\"fallback\":" << (fallback_triggered ? "true" : "false")
       << ",\"fallback_msg\":\"Precomputed indexes (CH/HL/ALT/Bidir) do not support dynamic traffic/avoidance/profiles. Fell back to A*.\""
       << ",\"src\":" << src << ",\"dst\":" << dst
       << ",\"srcCoord\":[" << g.coord(src).lat << "," << g.coord(src).lon << "]"
       << ",\"dstCoord\":[" << g.coord(dst).lat << "," << g.coord(dst).lon << "]"
       << ",\"nearest_ms\":" << nearest_ms;

    if (!alt_results.empty()) {
        ss << ",\"alternatives\":[";
        for (size_t i = 0; i < alt_results.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "{";
            write_route_fields(ss, g, alt_results[i], alt_query_times[i]);
            ss << "}";
        }
        ss << "]";
    }
    ss << "}";
    return ss.str();
}

// URL-decode
static std::string url_decode(const std::string& src) {
    std::string dst;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '%' && i + 2 < src.size()) {
            int val;
            std::istringstream iss(src.substr(i + 1, 2));
            if (iss >> std::hex >> val) { dst += static_cast<char>(val); i += 2; continue; }
        }
        if (src[i] == '+') { dst += ' '; continue; }
        dst += src[i];
    }
    return dst;
}

// Parse query string
static std::unordered_map<std::string, std::string> parse_query(const std::string& qs) {
    std::unordered_map<std::string, std::string> params;
    std::istringstream stream(qs);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            params[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
        }
    }
    return params;
}

// Read a file into string
static std::string read_file_str(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

// MIME type
static std::string mime_type(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".css"))  return "text/css";
    if (path.ends_with(".js"))   return "application/javascript";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".png"))  return "image/png";
    if (path.ends_with(".svg"))  return "image/svg+xml";
    if (path.ends_with(".ico"))  return "image/x-icon";
    return "text/plain";
}

static void send_response(socket_t client, int code, const std::string& content_type,
                           const std::string& body) {
    std::string status = (code == 200) ? "OK" : (code == 404) ? "Not Found" : "Error";
    std::ostringstream resp;
    resp << "HTTP/1.1 " << code << " " << status << "\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Connection: close\r\n"
         << "\r\n"
         << body;
    std::string s = resp.str();
    send(client, s.c_str(), static_cast<int>(s.size()), 0);
}

static int cmd_server(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: server <graph> [--ch <ch>] [--alt <alt>] [--hl <hl>] [--port <8080>] [--web <dir>]\n";
        return 1;
    }
    const std::string graph_path = args[0];
    const std::string ch_path    = get_arg(args, "--ch");
    const std::string alt_path   = get_arg(args, "--alt");
    const std::string hl_path    = get_arg(args, "--hl");
    const std::string web_dir    = get_arg(args, "--web", "web");
    const int         port       = std::stoi(get_arg(args, "--port", "8080"));

    std::cerr << "Loading graph...\n";
    Graph g = Graph::load(graph_path);
    std::cerr << "Graph: " << g.num_nodes() << " nodes, " << g.num_edges() << " edges\n";

    std::unique_ptr<CHGraph> ch;
    if (!ch_path.empty()) {
        std::cerr << "Loading CH...\n";
        ch = std::make_unique<CHGraph>(load_ch(ch_path, g));
        std::cerr << "CH loaded (" << ch->num_shortcuts << " shortcuts)\n";
    }

    std::unique_ptr<ALTData> alt;
    if (!alt_path.empty()) {
        std::cerr << "Loading ALT...\n";
        alt = std::make_unique<ALTData>(load_alt(alt_path));
        std::cerr << "ALT loaded (" << alt->num_landmarks << " landmarks)\n";
    }

    std::unique_ptr<HubLabels> hl;
    if (!hl_path.empty()) {
        std::cerr << "Loading HL...\n";
        hl = std::make_unique<HubLabels>(load_hub_labels(hl_path));
        std::cerr << "HL loaded (" << hl->num_nodes << " nodes)\n";
    }

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    socket_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { std::cerr << "Socket error\n"; return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Bind failed on port " << port << "\n";
        CLOSESOCK(srv);
        return 1;
    }

    listen(srv, 10);
    std::cerr << "\n============================================\n"
              << "  Map Routing Server running!\n"
              << "  Open: http://localhost:" << port << "\n"
              << "============================================\n\n";

    while (true) {
        sockaddr_in client_addr{};
        int client_len = sizeof(client_addr);
        socket_t client = accept(srv, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client == INVALID_SOCKET) continue;

        // Read request
        char buf[4096];
        int n = recv(client, buf, sizeof(buf) - 1, 0);
        if (n <= 0) { CLOSESOCK(client); continue; }
        buf[n] = '\0';
        std::string request(buf);

        // Parse method and path
        auto space1 = request.find(' ');
        auto space2 = request.find(' ', space1 + 1);
        if (space1 == std::string::npos || space2 == std::string::npos) {
            CLOSESOCK(client); continue;
        }
        std::string path = request.substr(space1 + 1, space2 - space1 - 1);

        // Split path and query string
        std::string query_str;
        auto qpos = path.find('?');
        if (qpos != std::string::npos) {
            query_str = path.substr(qpos + 1);
            path = path.substr(0, qpos);
        }

        // API routes
        if (path == "/api/info") {
            std::string json = graph_bounds_json(g);
            send_response(client, 200, "application/json", json);
        }
        else if (path == "/api/route") {
            auto params = parse_query(query_str);
            try {
                double srcLat = std::stod(params["srcLat"]);
                double srcLon = std::stod(params["srcLon"]);
                double dstLat = std::stod(params["dstLat"]);
                double dstLon = std::stod(params["dstLon"]);
                std::string algo = params.count("algo") ? params["algo"] : "dijkstra";
                bool alternatives = params.count("alternatives") && (params["alternatives"] == "true" || params["alternatives"] == "1");
                
                int time_of_day = params.count("time_of_day") ? std::stoi(params["time_of_day"]) : -1;
                std::string profile = params.count("profile") ? params["profile"] : "fastest";
                std::string avoidance_zones = params.count("avoidance_zones") ? params["avoidance_zones"] : "";

                std::string json = route_json(g, ch.get(), alt.get(), hl.get(), srcLat, srcLon, dstLat, dstLon, algo, alternatives, time_of_day, profile, avoidance_zones);
                std::cerr << "[Route] " << algo << (alternatives ? " (+alts)" : "") << ": (" << srcLat << "," << srcLon
                          << ") -> (" << dstLat << "," << dstLon << ")\n";
                send_response(client, 200, "application/json", json);
            } catch (...) {
                send_response(client, 400, "application/json",
                              "{\"error\":\"Invalid parameters\"}");
            }
        }
        else if (path == "/api/optimize") {
            auto params = parse_query(query_str);
            try {
                std::string coords_str = params["coords"];
                std::vector<LatLon> input_coords;
                size_t start = 0;
                while (start < coords_str.size()) {
                    size_t end = coords_str.find('|', start);
                    std::string pair_str = coords_str.substr(start, (end == std::string::npos ? std::string::npos : end - start));
                    auto comma = pair_str.find(',');
                    if (comma != std::string::npos) {
                        double lat = std::stod(pair_str.substr(0, comma));
                        double lon = std::stod(pair_str.substr(comma + 1));
                        input_coords.push_back({lat, lon});
                    }
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
                
                size_t k = input_coords.size();
                std::ostringstream ss;
                ss << "{\"optimized\":[";
                
                if (k <= 2) {
                    for (size_t i = 0; i < k; ++i) {
                        if (i > 0) ss << ",";
                        ss << i;
                    }
                    ss << "]}";
                    send_response(client, 200, "application/json", ss.str());
                } else {
                    std::vector<NodeId> nodes(k);
                    for (size_t i = 0; i < k; ++i) {
                        nodes[i] = find_nearest(g, input_coords[i].lat, input_coords[i].lon);
                    }
                    
                    std::vector<std::vector<Weight>> matrix(k, std::vector<Weight>(k, INF_WEIGHT));
                    for (size_t i = 0; i < k; ++i) {
                        for (size_t j = 0; j < k; ++j) {
                            if (i == j) { matrix[i][j] = 0; continue; }
                            RouteResult res;
                            if (hl && ch) {
                                res = hub_query_full(*hl, *ch, nodes[i], nodes[j]);
                            } else if (ch) {
                                res = ch_query(*ch, nodes[i], nodes[j]);
                            } else {
                                Dijkstra dijk(g);
                                res = dijk.query(nodes[i], nodes[j]);
                            }
                            matrix[i][j] = res.found ? res.cost : INF_WEIGHT;
                        }
                    }
                    
                    struct TSPSolver {
                        const std::vector<std::vector<Weight>>& matrix;
                        size_t k;
                        Weight best_cost{INF_WEIGHT};
                        std::vector<size_t> best_path;
                        
                        void search(size_t current_node, Weight current_cost, std::vector<size_t>& path, std::vector<bool>& visited) {
                            if (current_cost >= best_cost) return;
                            
                            if (path.size() == k - 1) {
                                Weight final_cost = current_cost + matrix[current_node][k - 1];
                                if (final_cost < best_cost) {
                                    best_cost = final_cost;
                                    best_path = path;
                                    best_path.push_back(k - 1);
                                }
                                return;
                            }
                            
                            for (size_t next = 1; next < k - 1; ++next) {
                                if (!visited[next]) {
                                    Weight step = matrix[current_node][next];
                                    if (step != INF_WEIGHT) {
                                        visited[next] = true;
                                        path.push_back(next);
                                        search(next, current_cost + step, path, visited);
                                        path.pop_back();
                                        visited[next] = false;
                                    }
                                }
                            }
                        }
                    };
                    
                    std::vector<size_t> path = {0};
                    std::vector<bool> visited(k, false);
                    visited[0] = true;
                    TSPSolver solver{matrix, k};
                    solver.search(0, 0, path, visited);
                    
                    if (solver.best_cost == INF_WEIGHT) {
                        for (size_t i = 0; i < k; ++i) {
                            if (i > 0) ss << ",";
                            ss << i;
                        }
                    } else {
                        for (size_t i = 0; i < solver.best_path.size(); ++i) {
                            if (i > 0) ss << ",";
                            ss << solver.best_path[i];
                        }
                    }
                    ss << "]}";
                    send_response(client, 200, "application/json", ss.str());
                }
            } catch (...) {
                send_response(client, 400, "application/json",
                              "{\"error\":\"Invalid optimize parameters\"}");
            }
        }
        else if (path == "/api/traffic") {
            auto params = parse_query(query_str);
            try {
                int time_of_day = params.count("time_of_day") ? std::stoi(params["time_of_day"]) : -1;
                
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(7);
                ss << "{\"congested\":[";
                
                if (time_of_day >= 0 && time_of_day < 1440) {
                    bool first = true;
                    uint32_t N = g.num_nodes();
                    for (NodeId u = 0; u < N; ++u) {
                        for (const auto& e : g.fwd(u)) {
                            double factor = get_edge_traffic_factor(g, u, e, time_of_day);
                            if (factor > 1.05) {
                                LatLon coord_u = g.coord(u);
                                LatLon coord_v = g.coord(e.to);
                                if (!first) ss << ",";
                                first = false;
                                ss << "{\"coords\":[[" << coord_u.lat << "," << coord_u.lon << "],["
                                   << coord_v.lat << "," << coord_v.lon << "]],\"factor\":" << std::setprecision(3) << factor << "}";
                            }
                        }
                    }
                }
                ss << "]}";
                send_response(client, 200, "application/json", ss.str());
            } catch (...) {
                send_response(client, 400, "application/json",
                              "{\"error\":\"Invalid traffic parameters\"}");
            }
        }
        else if (path == "/api/nearest") {
            auto params = parse_query(query_str);
            try {
                double lat = std::stod(params["lat"]);
                double lon = std::stod(params["lon"]);
                NodeId v = find_nearest(g, lat, lon);
                LatLon c = g.coord(v);
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(7);
                ss << "{\"node\":" << v << ",\"lat\":" << c.lat << ",\"lon\":" << c.lon << "}";
                send_response(client, 200, "application/json", ss.str());
            } catch (...) {
                send_response(client, 400, "application/json",
                              "{\"error\":\"Invalid parameters\"}");
            }
        }
        else if (path == "/api/isochrone") {
            auto params = parse_query(query_str);
            try {
                double lat = std::stod(params["lat"]);
                double lon = std::stod(params["lon"]);
                double max_time_min = std::stod(params["maxTime"]);
                
                NodeId src = find_nearest(g, lat, lon);
                Weight max_weight = static_cast<Weight>(max_time_min * 60.0 * 100.0); // min to centiseconds

                std::vector<Weight> cost(g.num_nodes(), INF_WEIGHT);
                std::vector<NodeId> touched;
                
                struct Entry {
                    Weight dist;
                    NodeId node;
                    bool operator>(const Entry& o) const { return dist > o.dist; }
                };
                std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

                cost[src] = 0;
                touched.push_back(src);
                pq.push({0, src});

                while (!pq.empty()) {
                    auto [d, u] = pq.top(); pq.pop();
                    if (d > cost[u]) continue;
                    if (d > max_weight) continue;

                    for (const auto& e : g.fwd(u)) {
                        Weight nd = d + e.weight;
                        if (nd < cost[e.to]) {
                            if (cost[e.to] == INF_WEIGHT) touched.push_back(e.to);
                            cost[e.to] = nd;
                            pq.push({nd, e.to});
                        }
                    }
                }

                // Group into buckets: 0-2m, 2-5m, 5-10m, 10-15m
                std::vector<std::vector<NodeId>> buckets(4);
                for (NodeId v : touched) {
                    if (cost[v] == INF_WEIGHT) continue;
                    double minutes = cost[v] / 100.0 / 60.0;
                    if (minutes <= 2.0) buckets[0].push_back(v);
                    else if (minutes <= 5.0) buckets[1].push_back(v);
                    else if (minutes <= 10.0) buckets[2].push_back(v);
                    else if (minutes <= 15.0) buckets[3].push_back(v);
                }

                std::ostringstream ss;
                ss << std::fixed << std::setprecision(7);
                ss << "{\"center\":[" << g.coord(src).lat << "," << g.coord(src).lon << "],\"buckets\":[";
                for (size_t b = 0; b < 4; ++b) {
                    if (b > 0) ss << ",";
                    ss << "[";
                    for (size_t i = 0; i < buckets[b].size(); ++i) {
                        if (i > 0) ss << ",";
                        LatLon coord = g.coord(buckets[b][i]);
                        ss << "[" << coord.lat << "," << coord.lon << "]";
                    }
                    ss << "]";
                }
                ss << "]}";
                send_response(client, 200, "application/json", ss.str());
            } catch (...) {
                send_response(client, 400, "application/json",
                              "{\"error\":\"Invalid parameters\"}");
            }
        }
        else if (path == "/api/network") {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(7);
            ss << "{\"nodes\":[";
            uint32_t N = g.num_nodes();
            for (uint32_t v = 0; v < N; ++v) {
                if (v > 0) ss << ",";
                LatLon coord = g.coord(v);
                ss << "[" << coord.lat << "," << coord.lon << "]";
            }
            ss << "]}";
            send_response(client, 200, "application/json", ss.str());
        }
        else {
            // Serve static files
            std::string file_path = web_dir;
            if (path == "/") path = "/index.html";
            file_path += path;
            std::string content = read_file_str(file_path);
            if (content.empty()) {
                send_response(client, 404, "text/plain", "Not Found");
            } else {
                send_response(client, 200, mime_type(file_path), content);
            }
        }

        CLOSESOCK(client);
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

// -- main ---------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Map Routing Engine\n"
                  << "Usage: map_routing <command> [options]\n"
                  << "Commands: parse, build, route, bench, info, server\n";
        return 1;
    }

    std::string_view cmd = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);

    try {
        if (cmd == "parse")  return cmd_parse(args);
        if (cmd == "build")  return cmd_build(args);
        if (cmd == "route")  return cmd_route(args);
        if (cmd == "bench")  return cmd_bench(args);
        if (cmd == "info")   return cmd_info(args);
        if (cmd == "server") return cmd_server(args);

        std::cerr << "Unknown command: " << cmd << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
