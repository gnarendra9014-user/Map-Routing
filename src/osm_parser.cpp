// ============================================================================
//  osm_parser.cpp — Two-pass libosmium PBF parser implementation
// ============================================================================
#include "osm_parser.hpp"
#include "geo.hpp"

// libosmium
#include <osmium/handler.hpp>
#include <osmium/io/pbf_input.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/visitor.hpp>
#include <osmium/index/map/flex_mem.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace routing {

// ── Helpers ──────────────────────────────────────────────────────────────────

static bool is_drivable(std::string_view hw, const ParserOptions& o) {
    if (hw == "motorway"      || hw == "motorway_link")  return o.include_motorway;
    if (hw == "trunk"         || hw == "trunk_link")     return o.include_trunk;
    if (hw == "primary"       || hw == "primary_link")   return o.include_primary;
    if (hw == "secondary"     || hw == "secondary_link") return o.include_secondary;
    if (hw == "tertiary"      || hw == "tertiary_link")  return o.include_tertiary;
    if (hw == "residential")                             return o.include_residential;
    if (hw == "unclassified")                            return o.include_unclassified;
    if (hw == "living_street")                           return o.include_living_street;
    if (hw == "service")                                 return o.include_service;
    if (hw == "road")                                    return o.include_road;
    return false;
}

// Determine oneway-ness.  Returns:
//   +1  → forward only (A→B)
//   -1  → reverse only (B→A, i.e. "oneway=-1")
//    0  → bidirectional
static int parse_oneway(const osmium::TagList& tags, std::string_view highway) noexcept {
    const char* ow = tags["oneway"];
    if (ow) {
        std::string_view v{ow};
        if (v == "yes" || v == "true" || v == "1") return +1;
        if (v == "-1"  || v == "reverse")           return -1;
        if (v == "no"  || v == "false" || v == "0") return  0;
    }
    // Motorways are implicitly one-way in OSM
    if (highway == "motorway" || highway == "motorway_link") return +1;
    return 0;
}

// ── Pass 1 handler: collect way metadata + required node IDs ────────────────

struct WayRecord {
    std::vector<int64_t> node_refs;
    double               speed_kmh{50.0};
    int                  oneway{0};  // -1, 0, +1
};

class Pass1Handler : public osmium::handler::Handler {
public:
    std::vector<WayRecord>         ways;
    std::unordered_set<int64_t>    required_nodes;
    const ParserOptions&           opts;

    explicit Pass1Handler(const ParserOptions& o) : opts(o) {}

    void way(const osmium::Way& way) {
        const char* hw_tag = way.tags()["highway"];
        if (!hw_tag) return;

        std::string_view hw{hw_tag};
        if (!is_drivable(hw, opts)) return;

        const auto& nodes = way.nodes();
        if (nodes.size() < 2) return;

        WayRecord rec;
        rec.node_refs.reserve(nodes.size());
        for (const auto& nr : nodes) {
            int64_t id = static_cast<int64_t>(nr.ref());
            rec.node_refs.push_back(id);
            required_nodes.insert(id);
        }

        // Speed
        const char* ms = way.tags()["maxspeed"];
        double spd = ms ? geo::parse_maxspeed(ms) : 0.0;
        rec.speed_kmh = (spd > 0.0) ? spd : geo::default_speed_kmh(hw);

        rec.oneway = parse_oneway(way.tags(), hw);
        ways.push_back(std::move(rec));
    }
};

// ── Pass 2 handler: collect coordinates for required nodes ──────────────────

class Pass2Handler : public osmium::handler::Handler {
public:
    const std::unordered_set<int64_t>& required;
    std::unordered_map<int64_t, std::pair<double,double>> coords; // id → (lat,lon)

    explicit Pass2Handler(const std::unordered_set<int64_t>& req)
        : required(req)
    {
        coords.reserve(req.size());
    }

    void node(const osmium::Node& n) {
        int64_t id = static_cast<int64_t>(n.id());
        if (required.count(id)) {
            coords[id] = { n.location().lat(), n.location().lon() };
        }
    }
};

// ── Public API ────────────────────────────────────────────────────────────────

size_t parse_osm_pbf(const std::string& pbf_path,
                     OsmData&           out,
                     const ParserOptions& opts)
{
    // ── Pass 1: read Ways ────────────────────────────────────────────────────
    std::cerr << "[Parser] Pass 1: scanning ways in " << pbf_path << "\n";

    Pass1Handler p1{opts};
    {
        osmium::io::File input_file{pbf_path, "pbf"};
        osmium::io::Reader reader{input_file, osmium::osm_entity_bits::way};
        osmium::apply(reader, p1);
        reader.close();
    }

    std::cerr << "[Parser] Found " << p1.ways.size()
              << " drivable ways, "
              << p1.required_nodes.size() << " unique node refs\n";

    // ── Pass 2: read Nodes ───────────────────────────────────────────────────
    std::cerr << "[Parser] Pass 2: reading node coordinates\n";

    Pass2Handler p2{p1.required_nodes};
    {
        osmium::io::File input_file{pbf_path, "pbf"};
        osmium::io::Reader reader{input_file, osmium::osm_entity_bits::node};
        osmium::apply(reader, p2);
        reader.close();
    }

    std::cerr << "[Parser] Retrieved " << p2.coords.size() << " node coordinates\n";

    // ── Build OsmData ────────────────────────────────────────────────────────
    // Index nodes
    out.nodes.reserve(p2.coords.size());
    out.node_index.reserve(p2.coords.size());

    for (const auto& [osm_id, latlon] : p2.coords) {
        size_t idx = out.nodes.size();
        out.nodes.push_back({ osm_id, latlon.first, latlon.second });
        out.node_index[osm_id] = idx;
    }

    // Build directed edges from way records
    size_t edge_count = 0;

    for (const auto& way : p1.ways) {
        const auto& refs = way.node_refs;

        for (size_t i = 0; i + 1 < refs.size(); ++i) {
            int64_t a = refs[i];
            int64_t b = refs[i + 1];

            // Both endpoints must have coordinates (some nodes may be outside
            // the file's bounding box or filtered)
            if (!p2.coords.count(a) || !p2.coords.count(b)) continue;

            // Emit directed edges according to oneway
            if (way.oneway >= 0) {          // forward edge A→B
                out.edges.push_back({ a, b, way.speed_kmh, (way.oneway != 0) });
                ++edge_count;
            }
            if (way.oneway <= 0) {          // backward edge B→A
                out.edges.push_back({ b, a, way.speed_kmh, (way.oneway != 0) });
                ++edge_count;
            }
        }
    }

    std::cerr << "[Parser] Emitted " << edge_count << " directed edges\n";
    return edge_count;
}

} // namespace routing
