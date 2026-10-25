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
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets ShaderTools HINTS /opt/homebrew/opt/qt /usr/local/opt/qt)
find_package(SQLite3 REQUIRED)
# ponytail: Qt 6.9.0's FindWrapOpenGL links AGL, which the macOS 26 SDK no longer has; drop it (delete this once Homebrew's
# Qt no longer links it)
if(TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_gl WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    list(FILTER _gl EXCLUDE REGEX "AGL")
    set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_gl}")
endif()

# ── Design database: libraries (one SQLite file each), cells, views (docs/rebuild.md, oadb) ──────────────────────
add_library(oadb STATIC oadb/oadb.cpp)
target_include_directories(oadb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(oadb PUBLIC SQLite::SQLite3)

# ── Command layer: every action a typed, validated, undoable command ─────────────────────────────────────────────
# ── Schematic semantics: placement, connectivity, symbol generation ────────────────────────────────────────────
add_library(openvlsi_sch STATIC sch/schematic.cpp)
target_link_libraries(openvlsi_sch PUBLIC oadb)

# ── Worker processes (a process group with a watchdog), and simulation: netlister, TreeSPICE runs, raw files ─────
add_library(openvlsi_proc STATIC flow/subprocess.cpp)
target_include_directories(openvlsi_proc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_library(openvlsi_sim STATIC sim/netlist.cpp sim/run.cpp)
target_link_libraries(openvlsi_sim PUBLIC openvlsi_sch openvlsi_proc Qt6::Core)

add_library(openvlsi_cmd STATIC cmd/registry.cpp cmd/core_commands.cpp cmd/schematic_commands.cpp cmd/sim_commands.cpp)
target_include_directories(openvlsi_cmd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openvlsi_cmd PUBLIC oadb openvlsi_sch openvlsi_sim Qt6::Core)

# ── MCP server: the command registry over stdio JSON-RPC ─────────────────────────────────────────────────────────
add_library(openvlsi_mcp STATIC mcp/server.cpp)
target_link_libraries(openvlsi_mcp PUBLIC openvlsi_cmd)
add_executable(openvlsi-mcp mcp/main.cpp)
target_link_libraries(openvlsi-mcp PRIVATE openvlsi_mcp)
# each executable that simulates finds treespice beside it
add_custom_command(TARGET openvlsi-mcp POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:treespice> $<TARGET_FILE_DIR:openvlsi-mcp>)
add_dependencies(openvlsi-mcp treespice)

# ── The app: the CIW and the tools' windows, every action a command through app::Session ───────────────────────────
set(CMAKE_AUTOMOC ON)
add_library(openvlsi_app STATIC app/session.cpp app/ciw.cpp app/library_manager.cpp app/scene.cpp app/canvas.cpp
            app/view_editor.cpp)
target_link_libraries(openvlsi_app PUBLIC openvlsi_cmd Qt6::Widgets PRIVATE Qt6::GuiPrivate)  # QRhi: <rhi/qrhi.h>
qt_add_shaders(openvlsi_app "canvas_shaders" PREFIX "/" BASE app FILES app/shaders/canvas.vert app/shaders/canvas.frag)
add_executable(OpenVLSI MACOSX_BUNDLE app/main.cpp)
target_link_libraries(OpenVLSI PRIVATE openvlsi_app)
add_custom_command(TARGET OpenVLSI POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:treespice> $<TARGET_FILE_DIR:OpenVLSI>)
add_dependencies(OpenVLSI treespice)
set_target_properties(OpenVLSI PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER org.openvlsi.OpenVLSI
                      MACOSX_BUNDLE_BUNDLE_NAME OpenVLSI MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

# ── Flow backends (Yosys / OpenROAD / KLayout drivers, report parsers) and the RV32I SoC ─────────────────────────
add_library(openvlsi_flow STATIC flow/yosys_log_parser.cpp flow/openroad_report_parser.cpp
            flow/klayout_report_parser.cpp flow/def_parser.cpp flow/rtl_flow.cpp)
target_include_directories(openvlsi_flow PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openvlsi_flow PUBLIC acutesim_engine openvlsi_proc)
add_library(openvlsi_soc STATIC soc/rv32i.cpp soc/soc.cpp)
target_include_directories(openvlsi_soc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(openvlsi_soc PUBLIC acutesim_engine)

# ── Tests ─────────────────────────────────────────────────────────────────────────────────────────────────────────
enable_testing()
foreach(t test_oadb test_commands test_mcp test_schematic test_simulation test_library_manager test_view_editor)
    add_executable(${t} tests/${t}.cpp)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
target_link_libraries(test_oadb PRIVATE oadb)
target_link_libraries(test_commands PRIVATE openvlsi_cmd)
target_link_libraries(test_schematic PRIVATE openvlsi_cmd)
target_link_libraries(test_simulation PRIVATE openvlsi_cmd)
target_compile_definitions(test_simulation PRIVATE OPENVLSI_TREESPICE="$<TARGET_FILE:treespice>")
add_dependencies(test_simulation treespice)
target_link_libraries(test_mcp PRIVATE openvlsi_mcp)
target_link_libraries(test_library_manager PRIVATE openvlsi_app)
target_link_libraries(test_view_editor PRIVATE openvlsi_app)
# the plugins of the Qt this links (a machine with a second, stale Qt prefix otherwise finds no platform plugin)
set_tests_properties(test_library_manager test_view_editor PROPERTIES ENVIRONMENT
                     "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/..")
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
