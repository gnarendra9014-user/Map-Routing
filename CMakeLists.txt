cmake_minimum_required(VERSION 3.20)
project(map_routing VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# === Build type defaults ======================================================
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# === Compiler flags ===========================================================
if(MSVC)
  # /W3 instead of /W4 (less pedantic for libosmium headers)
  # /EHsc required for exceptions in MSVC
  add_compile_options(/W3 /EHsc /O2)
  # Suppress noisy warnings from external headers
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS NOMINMAX)
else()
  add_compile_options(-Wall -Wextra -O3 -march=native)
endif()

# === vcpkg / find packages ====================================================
# libosmium is header-only; vcpkg does not ship FindOsmium.cmake,
# so we locate headers via find_path (as recommended by vcpkg usage file).
find_path(OSMIUM_INCLUDE_DIRS "osmium/version.hpp")
if(NOT OSMIUM_INCLUDE_DIRS)
  message(FATAL_ERROR "libosmium headers not found. Make sure libosmium is installed via vcpkg.")
endif()
message(STATUS "Found libosmium headers at: ${OSMIUM_INCLUDE_DIRS}")

find_package(ZLIB   REQUIRED)
find_package(BZip2  REQUIRED)

# Threads required by libosmium
find_package(Threads REQUIRED)

# protozero is also header-only
find_path(PROTOZERO_INCLUDE_DIRS "protozero/version.hpp")
if(NOT PROTOZERO_INCLUDE_DIRS)
  message(FATAL_ERROR "protozero headers not found. Make sure protozero is installed via vcpkg.")
endif()
message(STATUS "Found protozero headers at: ${PROTOZERO_INCLUDE_DIRS}")

# === Source files (static library) ============================================
set(LIB_SOURCES
  src/osm_parser.cpp
  src/graph.cpp
  src/dijkstra.cpp
  src/astar.cpp
  src/bidirectional_astar.cpp
  src/ch.cpp
  src/alt.cpp
  src/hub_labeling.cpp
  src/geojson.cpp
)

add_library(routing_lib STATIC ${LIB_SOURCES})
target_include_directories(routing_lib PUBLIC
  ${PROJECT_SOURCE_DIR}/include
  ${OSMIUM_INCLUDE_DIRS}
  ${PROTOZERO_INCLUDE_DIRS}
)
target_link_libraries(routing_lib
  PUBLIC
    ZLIB::ZLIB
    BZip2::BZip2
    Threads::Threads
)

# === Main CLI executable ======================================================
add_executable(map_routing tools/main.cpp)
target_link_libraries(map_routing PRIVATE routing_lib)

# Link Winsock for embedded HTTP server (Windows only)
if(WIN32)
  target_link_libraries(map_routing PRIVATE ws2_32)
endif()

# === Tests ====================================================================
enable_testing()

foreach(test_name
    test_parser
    test_graph
    test_routing
    test_ch
)
  add_executable(${test_name} tests/${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE routing_lib)
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
