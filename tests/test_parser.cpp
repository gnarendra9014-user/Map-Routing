// ============================================================================
//  test_parser.cpp — OSM Parser tests (no PBF file required)
//  We test the helper functions and OsmData construction directly.
// ============================================================================
#include "osm_parser.hpp"
#include "geo.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace routing;
using namespace routing::geo;

// ── Test: Haversine is symmetric and non-negative ─────────────────────────────
void test_haversine() {
    double d1 = haversine_m(52.5200, 13.4050, 48.8566, 2.3522); // Berlin→Paris
    double d2 = haversine_m(48.8566, 2.3522, 52.5200, 13.4050); // Paris→Berlin
    assert(std::abs(d1 - d2) < 1.0 && "Haversine must be symmetric");
    assert(d1 > 870000 && d1 < 890000 && "Berlin-Paris ~878km");
    assert(haversine_m(52.0, 13.0, 52.0, 13.0) == 0.0 && "Same point = 0");
    std::cout << "  [PASS] haversine: Berlin→Paris = " << d1/1000.0 << " km\n";
}

// ── Test: travel_time_cs is monotone ─────────────────────────────────────────
void test_travel_time() {
    Weight t1 = travel_time_cs(1000.0, 50.0);   // 1km at 50km/h = 72s = 7200 cs
    Weight t2 = travel_time_cs(1000.0, 100.0);  // 1km at 100km/h = 36s = 3600 cs
    assert(t1 > t2 && "Faster speed → lower travel time");
    assert(t1 == 7200 && "1km @ 50km/h = 7200 centiseconds");
    assert(t2 == 3600 && "1km @ 100km/h = 3600 centiseconds");
    std::cout << "  [PASS] travel_time_cs: 1km@50 = " << t1 << " cs\n";
}

// ── Test: maxspeed parser ─────────────────────────────────────────────────────
void test_maxspeed_parser() {
    assert(parse_maxspeed("50")      == 50.0);
    assert(parse_maxspeed("30 mph")  > 48.0 && parse_maxspeed("30 mph") < 50.0);
    assert(parse_maxspeed("DE:urban") == 50.0);
    assert(parse_maxspeed("walk")    == 5.0);
    assert(parse_maxspeed("")        == 0.0);
    std::cout << "  [PASS] maxspeed parser\n";
}

// ── Test: default_speed_kmh ───────────────────────────────────────────────────
void test_default_speed() {
    assert(default_speed_kmh("motorway")   >= 100.0);
    assert(default_speed_kmh("residential") < 50.0);
    assert(default_speed_kmh("living_street") <= 15.0);
    std::cout << "  [PASS] default_speed_kmh\n";
}

// ── Test: OsmData construction ────────────────────────────────────────────────
void test_osmdata_construction() {
    OsmData data;
    data.nodes = {
        { 1, 52.5, 13.4 },
        { 2, 52.6, 13.5 },
        { 3, 52.7, 13.6 },
    };
    data.node_index = {{ 1, 0 }, { 2, 1 }, { 3, 2 }};
    data.edges = {
        { 1, 2, 50.0, false },
        { 2, 1, 50.0, false },
        { 2, 3, 80.0, true  },
    };
    assert(data.nodes.size() == 3);
    assert(data.edges.size() == 3);
    assert(data.node_index.at(2) == 1);
    std::cout << "  [PASS] OsmData construction\n";
}

int main() {
    std::cout << "=== test_parser ===\n";
    test_haversine();
    test_travel_time();
    test_maxspeed_parser();
    test_default_speed();
    test_osmdata_construction();
    std::cout << "All parser tests passed.\n";
    return 0;
}
