cmake_minimum_required(VERSION 3.21)
project(OpenVLSI VERSION 2.0 LANGUAGES CXX)

# OpenVLSI: a macOS-first design suite (docs/rebuild.md). TreeSPICE (acutesim_engine/) is the simulator; the flow
# tools (Yosys, OpenROAD, KLayout, ...) are backends run as they are.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
if(WIN32)  # as the engine's CMakeLists: windows.h's min/max macros; M_PI needs _USE_MATH_DEFINES
    add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
endif()
if(MSVC)
    add_compile_options(/fp:strict)
else()
    add_compile_options(-fno-fast-math -frounding-math -ffp-contract=off)  # floating-point determinism
endif()

# GPU fronts in the engine (Dawn / WebGPU, Metal on macOS). Off by default: the app's foundation does not need it and
# Dawn dominates a clean build. ON provides dawn_native, which the engine picks up when built as a subdirectory.
option(OPENVLSI_GPU "Fetch and build Dawn for TreeSPICE's GPU fronts" OFF)
if(OPENVLSI_GPU)
    include(FetchContent)
    FetchContent_Declare(dawn GIT_REPOSITORY https://dawn.googlesource.com/dawn GIT_TAG chromium/6943
                         GIT_SHALLOW ON GIT_SUBMODULES "")
    foreach(opt DAWN_WERROR TINT_WERROR DAWN_BUILD_SAMPLES DAWN_USE_GLFW DAWN_USE_X11 DAWN_ENABLE_VULKAN
                DAWN_ENABLE_D3D12 DAWN_ENABLE_D3D11 DAWN_ENABLE_OPENGLES DAWN_ENABLE_DESKTOP_GL TINT_BUILD_TESTS
                DAWN_BUILD_TESTS)
        set(${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(DAWN_ENABLE_METAL ${APPLE} CACHE BOOL "" FORCE)
    set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(dawn)
endif()

add_subdirectory(acutesim_engine)

# Homebrew's full qt first: a machine with both qt and a newer qtbase otherwise mixes the two's plugins
find_package(Qt6 6.5 REQUIRED COMPONENTS Core HINTS /opt/homebrew/opt/qt /usr/local/opt/qt)
find_package(SQLite3 REQUIRED)

# ── Design database: libraries (one SQLite file each), cells, views (docs/rebuild.md, oadb) ──────────────────────
add_library(oadb STATIC oadb/oadb.cpp)
target_include_directories(oadb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(oadb PUBLIC SQLite::SQLite3)

# ── Command layer: every action a typed, validated, undoable command ─────────────────────────────────────────────
add_library(openvlsi_cmd STATIC cmd/registry.cpp cmd/core_commands.cpp)
target_include_directories(openvlsi_cmd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openvlsi_cmd PUBLIC oadb Qt6::Core)

# ── MCP server: the command registry over stdio JSON-RPC ─────────────────────────────────────────────────────────
add_library(openvlsi_mcp STATIC mcp/server.cpp)
target_link_libraries(openvlsi_mcp PUBLIC openvlsi_cmd)
add_executable(openvlsi-mcp mcp/main.cpp)
target_link_libraries(openvlsi-mcp PRIVATE openvlsi_mcp)

# ── Flow backends (Yosys / OpenROAD / KLayout drivers, report parsers) and the RV32I SoC ─────────────────────────
add_library(openvlsi_flow STATIC flow/yosys_log_parser.cpp flow/openroad_report_parser.cpp
            flow/klayout_report_parser.cpp flow/def_parser.cpp flow/rtl_flow.cpp flow/subprocess.cpp)
target_include_directories(openvlsi_flow PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openvlsi_flow PUBLIC acutesim_engine)
add_library(openvlsi_soc STATIC soc/rv32i.cpp soc/soc.cpp)
target_include_directories(openvlsi_soc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openvlsi_soc PUBLIC acutesim_engine)

# ── Tests ─────────────────────────────────────────────────────────────────────────────────────────────────────────
enable_testing()
foreach(t test_oadb test_commands test_mcp)
    add_executable(${t} tests/${t}.cpp)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
target_link_libraries(test_oadb PRIVATE oadb)
target_link_libraries(test_commands PRIVATE openvlsi_cmd)
target_link_libraries(test_mcp PRIVATE openvlsi_mcp)
target_compile_definitions(test_mcp PRIVATE OPENVLSI_MCP_EXE="$<TARGET_FILE:openvlsi-mcp>")

# The flow and SoC gates skip (and pass) without their tools: ~/eda (scripts/setup_eda_tools.sh), datasets/
foreach(t test_eda_drivers test_rtl_flow test_firmware_on_silicon test_soc_hello)
    add_executable(${t} tests/${t}.cpp)
    add_test(NAME ${t} COMMAND ${t})
    target_link_libraries(${t} PRIVATE openvlsi_flow openvlsi_soc)
    target_compile_definitions(${t} PRIVATE SOC_DATASETS="${CMAKE_CURRENT_SOURCE_DIR}/datasets")
endforeach()

add_test(NAME engine_isolation COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_engine_isolation.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
