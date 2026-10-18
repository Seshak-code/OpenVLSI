cmake_minimum_required(VERSION 3.16)
project(AcuteSim VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Phase 0.2.1: Floating-Point Determinism
option(ACUTESIM_DEPS_NOISY_WARNINGS "Enable verbose warnings for third-party dependencies" OFF)

if(NOT ACUTESIM_DEPS_NOISY_WARNINGS)
    set(CMAKE_WARN_DEPRECATED OFF)
    set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON)
endif()

if(MSVC)
    add_compile_options(/fp:strict)
else()
    # Common strictness
    add_compile_options(-fno-fast-math -frounding-math -ffp-contract=off)
    
    # GCC-specific: Prevent x87 excess precision
    if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
        add_compile_options(-ffloat-store)
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error -Wno-narrowing")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error")
endif()

# Platform-aware default Qt prefix hints (can be overridden by CMAKE_PREFIX_PATH or Qt6_DIR)
if(NOT DEFINED Qt6_DIR)
    if(APPLE)
        set(CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt")
        set(Qt6_DIR "/opt/homebrew/opt/qt/lib/cmake/Qt6")
        set(CMAKE_BUILD_RPATH "/opt/homebrew/opt/qt/lib")
        set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH};/opt/homebrew/opt/qt/lib")
        message(STATUS "Mac-specific CMAKE_PREFIX_PATH (unified qt 6.9.0) forced to: ${CMAKE_PREFIX_PATH}")
    elseif(WIN32 AND NOT DEFINED CMAKE_PREFIX_PATH)
        # On Windows, try to detect a Qt installation under C:/Qt and append likely cmake folders.
        set(_qt_root "C:/Qt")
        if(EXISTS "${_qt_root}")
            file(GLOB QT_ROOT_DIRS LIST_DIRECTORIES true "${_qt_root}/*")
            foreach(_d IN LISTS QT_ROOT_DIRS)
                if(IS_DIRECTORY "${_d}")
                    foreach(_sub IN ITEMS "" "/msvc2019_64" "/msvc2022_64" "/msvc2017_64" "/mingw81_64" "/mingw64")
                        set(_cmake_dir "${_d}${_sub}/lib/cmake")
                        if(EXISTS "${_cmake_dir}")
                            list(APPEND CMAKE_PREFIX_PATH "${_cmake_dir}")
                            message(STATUS "Appending Qt candidate to CMAKE_PREFIX_PATH: ${_cmake_dir}")
                        endif()
                    endforeach()
                endif()
            endforeach()
        else()
            message(STATUS "Qt root ${_qt_root} not found; please set Qt6_DIR or CMAKE_PREFIX_PATH to your Qt installation.")
        endif()
    endif()
endif()

# If Qt6_DIR still not defined, try to locate Qt6CoreConfig.cmake under C:/Qt and set Qt6_DIR
if(WIN32 AND NOT DEFINED Qt6_DIR)
    set(_found_qt_dir "")
    # common locations: C:/Qt/<version>/<compiler>/lib/cmake/Qt6Core
    file(GLOB_RECURSE _qt_core_configs LIST_DIRECTORIES false "C:/Qt/*/lib/cmake/Qt6Core/Qt6CoreConfig.cmake" "C:/Qt/*/*/lib/cmake/Qt6Core/Qt6CoreConfig.cmake")
    if(_qt_core_configs)
        list(GET _qt_core_configs 0 _first_qtconfig)
        get_filename_component(_found_qt_dir "${_first_qtconfig}" DIRECTORY)
        set(Qt6_DIR "${_found_qt_dir}")
        message(STATUS "Auto-detected Qt6_DIR = ${Qt6_DIR}")
    else()
        message(STATUS "No Qt6Config found automatically under C:/Qt. You can set -DQt6_DIR=C:/Qt/<version>/<compiler>/lib/cmake/Qt6Core")
    endif()
endif()

# If the user has a known Qt installation path, prefer that (useful when automatic detection fails)
# Removed hardcoded fallback to allow presets to control the path.

# Allow user to pass Qt6_DIR or CMAKE_PREFIX_PATH on command line or via CMake presets
if(DEFINED Qt6_DIR)
    message(STATUS "Using Qt6_DIR hint: ${Qt6_DIR}")
endif()
message(STATUS "CMAKE_PREFIX_PATH = ${CMAKE_PREFIX_PATH}")

# Emscripten-specific configuration
if(EMSCRIPTEN)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
    
    # Workaround for Qt 6.9+ Wasm build issue where _qt_test_emscripten_version is undefined
    macro(_qt_test_emscripten_version)
    endmacro()

    option(ACUTESIM_WASM_THREADS "Enable threading for WASM build" OFF)
endif()

# Global include directory (Project Root)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
# TreeSPICE (submodule acutesim_engine/) owns the public engine API: "engine_api/simulation_dto.h"
# resolves there when this repo's app-side engine_api/ does not have the header.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/acutesim_engine ${CMAKE_CURRENT_SOURCE_DIR}/acutesim_engine/engine_api)

if(APPLE AND NOT EMSCRIPTEN)
    # Pre-define WrapOpenGL::WrapOpenGL to force TBD linking.
    if(NOT TARGET WrapOpenGL::WrapOpenGL)
        add_library(WrapOpenGL::WrapOpenGL INTERFACE IMPORTED)
        # Link to the TBD stub in CommandLineTools SDK which is valid on this system
        target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/AGL.framework/Versions/Current/AGL.tbd")
        message(STATUS "Pre-defined WrapOpenGL::WrapOpenGL to link: AGL.tbd")
    endif()
endif()

# Find Qt6 (required)
if(EMSCRIPTEN)
    find_package(Qt6 COMPONENTS Core Gui Widgets Test REQUIRED)
    find_package(Qt6 COMPONENTS Svg QUIET)
else()
    # Aggressively force use of full 'qt' installation to avoid qtbase 6.9.3 collisions
    set(Qt6_DIR "/opt/homebrew/opt/qt/lib/cmake/Qt6")
    set(Qt6Core_DIR "/opt/homebrew/opt/qt/lib/cmake/Qt6Core")
    set(Qt6Gui_DIR "/opt/homebrew/opt/qt/lib/cmake/Qt6Gui")
    set(Qt6Widgets_DIR "/opt/homebrew/opt/qt/lib/cmake/Qt6Widgets")
    set(Qt6Test_DIR "/opt/homebrew/opt/qt/lib/cmake/Qt6Test")
    set(Qt6Concurrent_DIR "/opt/homebrew/opt/qt/lib/cmake/Qt6Concurrent")
    set(Qt6Svg_DIR "/opt/homebrew/opt/qt/lib/cmake/Qt6Svg")
    set(Qt6Concurrent_DIR "/opt/homebrew/opt/qt/lib/cmake/Qt6Concurrent")
    
    find_package(Qt6 COMPONENTS Core Gui Widgets Test Concurrent Svg REQUIRED)
endif()

if(NOT EMSCRIPTEN)
    add_definitions(-DACUTESIM_THREADS_ENABLED)
endif()

find_package(OpenMP)

if(NOT EMSCRIPTEN)
    
    # Phase 12: Orchestration Dependencies (Provided by vcpkg or system)
    find_package(protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
    find_package(absl CONFIG REQUIRED)

    # Phase 1.1: Integrate Google Dawn (GPU Acceleration)
    # Moved after gRPC to avoid Abseil target conflicts
    
    if(NOT ACUTESIM_DEPS_NOISY_WARNINGS)
        set(CMAKE_WARN_DEPRECATED OFF)
        # Suppress warnings from Dawn/Tint/Abseil
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
    endif()

    include(FetchContent)
    message(STATUS "Integrating Google Dawn (Native WebGPU Backend)...")
    
    set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
    
    FetchContent_Declare(
        dawn
        GIT_REPOSITORY https://dawn.googlesource.com/dawn
        GIT_TAG        chromium/6943
        GIT_SHALLOW    ON
        GIT_PROGRESS   ON
        GIT_SUBMODULES "" 
    )
    set(DAWN_WERROR OFF CACHE BOOL "" FORCE)
    set(TINT_WERROR OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    if(APPLE)  # elsewhere only the Null backend (the hosted CI runners have no GPU)
        set(DAWN_ENABLE_METAL ON CACHE BOOL "" FORCE)
    else()
        set(DAWN_ENABLE_METAL OFF CACHE BOOL "" FORCE)
    endif()
    set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(DAWN_USE_GLFW OFF CACHE BOOL "" FORCE)  # only Dawn's samples use it
    set(DAWN_USE_X11 OFF CACHE BOOL "" FORCE)   # window surfaces only (Vulkan is off)
    set(DAWN_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_D3D12 OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_D3D11 OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_OPENGLES OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE BOOL "" FORCE)
    set(DAWN_WERROR OFF)
    set(TINT_WERROR OFF)

    if(NOT ACUTESIM_DEPS_NOISY_WARNINGS)
        set(CMAKE_WARN_DEPRECATED OFF) 
    endif()
    
    FetchContent_MakeAvailable(dawn)
    
    if(NOT ACUTESIM_DEPS_NOISY_WARNINGS)
        set(CMAKE_WARN_DEPRECATED OFF) 
    endif()
    
    if(TARGET dawn_internal_config AND NOT MSVC)  # a GCC/Clang flag
        target_compile_options(dawn_internal_config INTERFACE -Wno-error)
    endif()

    set(DAWN_INTERNAL_INCLUDES 
        ${dawn_SOURCE_DIR}
        ${dawn_SOURCE_DIR}/src 
        ${dawn_BINARY_DIR}/gen/src
        ${dawn_SOURCE_DIR}/third_party/abseil-cpp
        # Dawn's Instance.h uses partition_alloc
        ${dawn_SOURCE_DIR}/third_party/partition_alloc
    )
endif()

# qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Create executable
set(ACUTESIM_SOURCES
    gui/core/mainwindow.cpp
    gui/core/mainwindow.h
    gui/core/layout_manager.cpp
    gui/core/layout_manager.h
    gui/core/storage_manager.cpp
    gui/core/storage_manager.h
    gui/core/docking_manager.cpp
    gui/core/docking_manager.cpp
    gui/core/docking_manager.h
    gui/core/platform_helper.cpp
    gui/core/platform_helper.h
    gui/core/activity_bar.cpp
    gui/core/activity_bar.h
    gui/core/keybinding_manager.cpp
    gui/core/keybinding_manager.h
    gui/core/keybinding_manager.h
    gui/core/icon_provider.cpp
    gui/core/icon_provider.h
    gui/core/editor_manager.cpp
    gui/core/editor_manager.h
    gui/core/editor_factory.cpp
    gui/core/editor_factory.h
    gui/core/hierarchy_path.cpp
    gui/core/hierarchy_path.h
    resources/icons.qrc
    gui/editors/schematic/schematic_editor.cpp
    gui/editors/schematic/schematic_editor_sync.cpp
    components/netlist_export.cpp
    gui/editors/schematic/schematic_editor.h
    gui/editors/code_editor.cpp
    gui/editors/code_editor.h
    gui/editors/json_viewer.cpp
    gui/editors/json_viewer.h
    gui/editors/image_viewer.cpp
    gui/editors/image_viewer.h
    gui/editors/hex_viewer.cpp
    gui/editors/hex_viewer.h
    gui/core/sim_auto_detector.cpp
    gui/core/sim_auto_detector.h
    gui/editors/schematic/schematic_view.cpp
    gui/editors/schematic/schematic_view.h
    gui/editors/component_wizard.cpp
    gui/editors/component_wizard.h
    gui/components/visual_component.cpp
    gui/components/visual_component.h
    gui/components/probe_tip.cpp
    gui/components/probe_tip.h

    gui/widgets/component_library.cpp
    gui/widgets/component_library.h
    gui/widgets/breadcrumb_widget.cpp
    gui/widgets/breadcrumb_widget.h
    gui/widgets/compliance_report_widget.cpp
    gui/widgets/compliance_report_widget.h
    gui/widgets/compliance_report_widget.h
    gui/analysis/pdk_compliance.cpp
    gui/analysis/pdk_compliance.h
    gui/analysis/pdk_manager.cpp
    gui/analysis/pdk_manager.h
    components/standard_cells.cpp
    gui/editors/schematic/schematic_editor_api.cpp
    gui/analysis/system_inspector.cpp
    gui/analysis/system_inspector.h
    gui/widgets/design_flow.cpp
    gui/widgets/design_flow.h
    gui/widgets/problems_panel.cpp
    gui/widgets/problems_panel.h
    gui/widgets/debug_suggestions_panel.cpp
    gui/widgets/debug_suggestions_panel.h
    gui/analysis/logic_analyzer.cpp
    gui/analysis/logic_analyzer.h
    gui/analysis/simulation_explorer.cpp
    gui/analysis/simulation_explorer.h
    gui/analysis/simulation_workspace.cpp
    gui/analysis/simulation_workspace.h
    gui/analysis/simulation_driver_adapter.h
    gui/analysis/monte_carlo_setup_widget.cpp
    gui/analysis/monte_carlo_setup_widget.h
    gui/analysis/statistical_dashboard.cpp
    gui/analysis/statistical_dashboard.h
    gui/analysis/variables_panel.cpp
    gui/analysis/variables_panel.h
    gui/analysis/calculator_widget.cpp
    gui/analysis/calculator_widget.h
    gui/analysis/parametric_sweep_dialog.cpp
    gui/analysis/parametric_sweep_dialog.h
    gui/analysis/simulation_profile.h
    gui/ai/ai_action_history.cpp
    gui/ai/ai_action_history.h
    gui/ai/ai_action_envelope.h
    gui/core/welcome_widget.cpp
    gui/core/welcome_widget.h
    ai_api/system/schema_api.cpp
    ai_api/system/schema_api.h
    ai_api/context/context_engine.cpp
    ai_api/context/context_engine.h
    ai_api/simulation/simulation_api.h
    ai_api/simulation/simulation_trace.cpp
    ai_api/simulation/simulation_trace.h
    ai_api/simulation/simulation_trace.h
    ai_api/context/prompt_builder.cpp
    ai_api/context/prompt_builder.h
    ai_api/context/rag_context.cpp
    ai_api/context/rag_context.h
    ai_api/analysis/topology_analyzer.cpp
    ai_api/analysis/topology_analyzer.h
    ai_api/system/feedback_loop.cpp
    ai_api/system/feedback_loop.h
    ai_api/system/feedback_loop.h
    ai_api/context/context_budget.cpp
    ai_api/context/context_budget.h
    ai_api/adk/tool_registry.cpp
    ai_api/adk/tool_registry.h
    ai_api/adk/tool_handlers.cpp
    ai_api/adk/tool_handlers.h
    ai_api/adk/session_manager.cpp
    ai_api/adk/session_manager.h
    ai_api/pipeline/rag_pipeline.cpp
    ai_api/pipeline/rag_pipeline.h
    gui/orchestration/worker_manager.cpp
    gui/orchestration/worker_manager.h
    gui/orchestration/pdk_manager.cpp
    gui/orchestration/pdk_manager.h
    gui/orchestration/pdk_browser_panel.cpp
    gui/orchestration/pdk_browser_panel.h
    gui/orchestration/worker_status_panel.cpp
    gui/orchestration/worker_status_panel.h
    gui/orchestration/rtl_pipeline_panel.cpp
    gui/orchestration/rtl_pipeline_panel.h
    gui/orchestration/synthesis_result_dialog.cpp
    gui/orchestration/synthesis_result_dialog.h
    gui/orchestration/pnr_result_dialog.cpp
    gui/orchestration/pnr_result_dialog.h
    gui/orchestration/signoff_result_dialog.cpp
    gui/orchestration/signoff_result_dialog.h
    gui/editors/physical_overlay_item.cpp
    gui/editors/physical_overlay_item.h
    gui/widgets/cell_pie_chart.h
    gui/widgets/timing_indicator.h
    gui/analysis/corner_setup_widget.cpp
    gui/analysis/corner_setup_widget.h
    gui/widgets/trust_badge_widget.cpp
    gui/widgets/trust_badge_widget.h
    gui/widgets/terminal_widget.cpp
    gui/widgets/terminal_widget.h
    gui/widgets/output_dock_widget.cpp
    gui/widgets/output_dock_widget.h
    gui/widgets/waveform_widget.cpp
    gui/widgets/waveform_widget.h
    gui/widgets/command_palette.cpp
    gui/widgets/command_palette.h
    gui/widgets/engine_log_panel.cpp
    gui/widgets/engine_log_panel.h
    gui/widgets/engine_console.cpp
    gui/widgets/engine_console.h
    gui/styling/theme_manager.cpp
    gui/styling/theme_manager.h
    gui/panels/ai_assistant_panel.cpp
    gui/panels/ai_assistant_panel.h
    gui/widgets/ghost_diff_viewer.cpp
    gui/widgets/ghost_diff_viewer.h
    gui/widgets/toast_manager.cpp
    gui/widgets/toast_manager.h
    gui/widgets/keybindings_dialog.cpp
    gui/widgets/keybindings_dialog.h
    gui/widgets/inline_dialog_types.h
    gui/widgets/inline_dialog.cpp
    gui/widgets/inline_dialog.h
    gui/widgets/inline_progress.cpp
    gui/widgets/inline_progress.h
    gui/widgets/project_model.cpp
    gui/widgets/project_model.h
    gui/widgets/library_model.cpp
    gui/widgets/library_model.h
    gui/widgets/project_explorer.cpp
    gui/widgets/project_explorer.h
    gui/widgets/search_dock.cpp
    gui/widgets/search_dock.h
    acutesim_engine/netlist/circuit.h
    components/design_db.cpp
    gui/orchestration/library_index.cpp
    gui/orchestration/library_index.h
    gui/scripting/tcl_bridge.cpp
    gui/scripting/tcl_bridge.h
    gui/core/subcircuit_resolver.cpp
    gui/components/standard_cell_visualizer.cpp
)

# ============================================================================
# ============================================================================
# acutesim_math_core — Header-only math + autodiff INTERFACE library
# No compiled sources; gives named CMake target to compute/math/ and autodiff/
# ============================================================================
add_library(acutesim_math_core INTERFACE)
target_include_directories(acutesim_math_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/autodiff
)

# acutesim_compute_core — Components + Compute kernel STATIC library
# Links acutesim_engine (canonical engine library)
# ============================================================================
add_library(acutesim_compute_core STATIC
    components/veriloga_generator.cpp
    compute/orchestration/drivers/def_parser.cpp
)
target_include_directories(acutesim_compute_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/acutesim_engine
    ${CMAKE_CURRENT_SOURCE_DIR}/acutesim_engine/netlist
    ${CMAKE_CURRENT_SOURCE_DIR}/acutesim_engine/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/acutesim_engine/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/acutesim_engine/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/acutesim_engine/infrastructure
)
target_link_libraries(acutesim_compute_core PUBLIC acutesim_engine)
target_compile_features(acutesim_compute_core PUBLIC cxx_std_17)

# ============================================================================
# acutesim_sim_layer — Orchestration drivers + compute_core (RENAMED from acutesim_engine)
# ============================================================================
if(NOT EMSCRIPTEN)
    add_library(acutesim_sim_layer STATIC
        compute/orchestration/simulation_context.cpp
        compute/orchestration/driver/local_worker_driver.cpp
        compute/orchestration/driver/batch_execution_engine.cpp
        compute/orchestration/driver/job_scheduler.cpp
        compute/orchestration/driver/veriloga_parser.cpp
        compute/orchestration/server/subprocess.cpp
    )
    target_include_directories(acutesim_sim_layer PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/dto
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/driver
    )
    target_link_libraries(acutesim_sim_layer PUBLIC acutesim_compute_core acutesim_engine)
    target_compile_features(acutesim_sim_layer PUBLIC cxx_std_17)
endif()

# ============================================================================
# acutesim_engine_api — Clean public API INTERFACE library
# Exposes ONLY engine_api/ headers (POD structs, enums, pure abstract interfaces).
# GUI and test targets link this for the stable contract; never the engine internals.
# ============================================================================
add_library(acutesim_engine_api INTERFACE)
target_include_directories(acutesim_engine_api INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine_api                   # app-side: drivers, job manager, results DB
    ${CMAKE_CURRENT_SOURCE_DIR}/acutesim_engine/engine_api   # TreeSPICE: bare "simulation_dto.h"
)

# ============================================================================
# acutesim_orchestration — Orchestration driver + EDA task STATIC library
# PUBLIC include: compute/orchestration/ (exposes dto/ and driver/ headers)
# PRIVATE link: acutesim_engine (transitive: compute_core + engine drivers)
# ============================================================================
if(NOT EMSCRIPTEN)
    add_library(acutesim_orchestration STATIC
        compute/orchestration/drivers/yosys_log_parser.cpp
        compute/orchestration/drivers/openroad_report_parser.cpp
        compute/orchestration/drivers/klayout_report_parser.cpp
        compute/orchestration/drivers/rtl_flow.cpp
        compute/orchestration/proto_mapper.cpp
        compute/orchestration/driver/remote_grpc_driver.cpp
        compute/orchestration/server/simulation_service_impl.cpp
    )
    target_include_directories(acutesim_orchestration PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
    )
    target_link_libraries(acutesim_orchestration
        PRIVATE acutesim_sim_layer
        PUBLIC Qt6::Core
    )
    target_compile_features(acutesim_orchestration PUBLIC cxx_std_17)

    find_package(Protobuf REQUIRED)
    find_package(gRPC REQUIRED)
    find_program(gRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

    set(SIM_PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/proto")
    set(SIM_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/compute/orchestration/proto")
    file(MAKE_DIRECTORY ${SIM_PROTO_GEN_DIR})

    set(SIM_PROTO_FILES "${SIM_PROTO_SRC_DIR}/simulation_service.proto")
    set(SIM_PB_SRCS "${SIM_PROTO_GEN_DIR}/simulation_service.pb.cc")
    set(SIM_PB_HDRS "${SIM_PROTO_GEN_DIR}/simulation_service.pb.h")
    set(SIM_GRPC_SRCS "${SIM_PROTO_GEN_DIR}/simulation_service.grpc.pb.cc")
    set(SIM_GRPC_HDRS "${SIM_PROTO_GEN_DIR}/simulation_service.grpc.pb.h")

    add_custom_command(
        OUTPUT ${SIM_PB_SRCS} ${SIM_PB_HDRS} ${SIM_GRPC_SRCS} ${SIM_GRPC_HDRS}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        ARGS --grpc_out=${SIM_PROTO_GEN_DIR}
             --cpp_out=${SIM_PROTO_GEN_DIR}
             -I ${SIM_PROTO_SRC_DIR}
             --plugin=protoc-gen-grpc=${gRPC_CPP_PLUGIN_EXECUTABLE}
             ${SIM_PROTO_FILES}
        DEPENDS ${SIM_PROTO_FILES}
    )

    add_library(acutesim_simulation_proto STATIC
        ${SIM_PB_SRCS}
        ${SIM_GRPC_SRCS}
    )

    target_link_libraries(acutesim_simulation_proto
        PUBLIC gRPC::grpc++
        PUBLIC protobuf::libprotobuf
    )

    target_include_directories(acutesim_simulation_proto
        PUBLIC ${SIM_PROTO_GEN_DIR}
    )

    target_link_libraries(acutesim_orchestration
        PRIVATE acutesim_simulation_proto
    )
endif()

# ===========================================================================
# acutesim_engine — standalone simulation library (link-time sovereignty)
# ===========================================================================
add_subdirectory(acutesim_engine)

qt_add_executable(${PROJECT_NAME} main.cpp ${ACUTESIM_SOURCES})

# Enforce strict GUI -> Physics layering via Python static check
add_custom_target(check_layering
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_layering.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Enforcing GUI -> Physics layer decoupling"
    VERBATIM
)
add_dependencies(${PROJECT_NAME} check_layering)

# Enforce engine isolation (Qt-free engine + no GUI bypass of engine_api/)
add_custom_target(check_engine_isolation
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_engine_isolation.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Enforcing bidirectional engine isolation (Qt-free engine + GUI/engine_api boundary)"
    VERBATIM
)
add_dependencies(${PROJECT_NAME} check_engine_isolation)

# ABI Stability Test — verifies no GUI recompile after engine-only patch
# Run explicitly after a full build: cmake --build build --target abi_stability_test
# NOT wired as add_dependencies (needs prior full build to fingerprint against)
if(NOT EMSCRIPTEN)
    add_custom_target(abi_stability_test
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/abi_stability_test.py
                        --skip-configure
                        --jobs ${CMAKE_BUILD_PARALLEL_LEVEL}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "ABI stability: patch engine internal, verify 0 GUI files recompile"
        VERBATIM
    )
endif()


# Phase 12: Orchestration Library (gRPC Worker Client)
if(NOT EMSCRIPTEN)
    # Generate C++ sources from Proto
    get_target_property(grpc_cpp_plugin gRPC::grpc_cpp_plugin LOCATION)
    
    protobuf_generate(TARGET ${PROJECT_NAME} 
        LANGUAGE cpp 
        PROTOS compute/orchestration/proto/worker_service.proto
        IMPORT_DIRS compute/orchestration/proto
    )
    protobuf_generate(TARGET ${PROJECT_NAME}
        LANGUAGE grpc
        GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
        PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin}"
        PROTOS compute/orchestration/proto/worker_service.proto
        IMPORT_DIRS compute/orchestration/proto
    )
    
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        gRPC::grpc++ 
        protobuf::libprotobuf
        absl::cord
        absl::strings
        absl::time
        absl::synchronization
        absl::status
    )
    
    # Enable defining the WorkerClient implementation
    target_compile_definitions(${PROJECT_NAME} PRIVATE ACUTESIM_ENABLE_ORCHESTRATION)
    target_sources(${PROJECT_NAME} PRIVATE compute/orchestration/grpc_worker_client.cpp)
endif()

if(EMSCRIPTEN)
    target_sources(${PROJECT_NAME} PRIVATE compute/orchestration/wasm_worker_client.cpp compute/orchestration/wasm_stubs.cpp)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors/schematic
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/styling
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/core
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/adk
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

if(NOT EMSCRIPTEN)
    if(WIN32)  # vcpkg's Tcl 9: FindTCL only knows the 8.x library names
        find_library(TCL_LIBRARY NAMES tcl90 tcl90t)
    endif()
    find_package(TCL)  # not REQUIRED: that also demands Tk, which the app does not use
    if(NOT TCL_FOUND)
        message(FATAL_ERROR "Tcl not found (headers and library)")
    endif()
    target_include_directories(${PROJECT_NAME} PRIVATE ${TCL_INCLUDE_PATH})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${TCL_LIBRARY})
else()
    # On WASM, we use the internal mock Tcl or just disable it
    target_compile_definitions(${PROJECT_NAME} PRIVATE TEST_MODE)
endif()

# Link Qt libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
# Link compute/orchestration libraries (replaces direct source compilation)
if(NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PRIVATE acutesim_engine_api acutesim_orchestration)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE acutesim_engine_api acutesim_compute_core)
endif()
# Enforce GUI/physics boundary at compile time
target_compile_definitions(${PROJECT_NAME} PRIVATE ACUTESIM_GUI_BUILD)

# Link Google Dawn (Native)
if(NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        dawn_native 
        dawn_proc
        webgpu_cpp
    )
    # Main application should only use public Dawn headers
    target_compile_definitions(${PROJECT_NAME} PRIVATE ACUTESIM_HAS_DAWN_NATIVE)
endif()

# Optional: Link Qt6::Svg if available
if(Qt6Svg_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Svg)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ACUTESIM_HAS_SVG)
endif()

if(NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Concurrent acutesim_soc)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SOC_DATASETS="${CMAKE_CURRENT_SOURCE_DIR}/datasets")
else()
    # Export runtime methods but do NOT generate HTML (we manage it manually)
    set_target_properties(${PROJECT_NAME} PROPERTIES 
        LINK_OPTIONS "SHELL:-s ALLOW_MEMORY_GROWTH=1 -s ASSERTIONS=1"
    )
endif()

# ===========================================================================
# Link acutesim_engine — AcuteSim depends on engine, not the other way round.
# acutesim_engine is PRIVATE: its internals (Dawn, AVX kernels) don't propagate.
# engine_api/ headers propagate via the PUBLIC include set on acutesim_engine.
# ===========================================================================
target_link_libraries(${PROJECT_NAME} PRIVATE acutesim_engine)


# Link OpenMP only if found (and not Wasm)
# Link OpenMP
if(OpenMP_CXX_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenMP::OpenMP_CXX)
endif()

# WebAssembly specific link flags
if(EMSCRIPTEN)
    target_link_options(${PROJECT_NAME} PRIVATE 
        "--bind" 
        "-sWASM=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sERROR_ON_UNDEFINED_SYMBOLS=0"
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=AcuteSim"
        "-sEXPORTED_RUNTIME_METHODS=specialHTMLTargets,UTF8ToString,UTF16ToString,stringToUTF8,stringToUTF16,ccall,cwrap,wasmMemory,FS,callMain,ENV,noExitRuntime,printErr"
        "-sEXPORTED_FUNCTIONS=['_main','_dumpStorageFile','_malloc','_free']"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/resources@/resources"
    )
    


    if(ACUTESIM_WASM_THREADS)
        target_link_options(${PROJECT_NAME} PRIVATE "-pthread" "-sUSE_PTHREADS=1" "-sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency")
        add_compile_options("-pthread")
        target_compile_definitions(${PROJECT_NAME} PRIVATE ACUTESIM_THREADS_ENABLED)
    endif()

    # Qt 6.9.3 wasm_singlethread was built WITHOUT -fwasm-exceptions (wasm_exceptions is in
    # QT_DISABLED_PUBLIC_FEATURES). Using old-style Emscripten exception handling (invoke_)
    # to match Qt's ABI. -fwasm-exceptions and -sSUPPORT_LONGJMP=wasm must NOT be used.
    target_link_options(${PROJECT_NAME} PRIVATE
        "-sPROXY_TO_WORKER=0"
    )
    # Force error on undefined symbols to 0 at the end
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sERROR_ON_UNDEFINED_SYMBOLS=0")
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".js")
    # Ensure the output filename is predictable for the HTML shell
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "AcuteSim")
endif()

# Set output name for all platforms (not just Emscripten) if needed, 
# although it already follows PROJECT_NAME.
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "AcuteSim")

# macOS specific: Ensure system frameworks like AGL are found
# macOS specific: Ensure system frameworks like AGL are found
if(APPLE)
    # AGL workaround is handled via WrapOpenGL pre-definition above
endif()

# Install rule
install(TARGETS ${PROJECT_NAME} 
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# Regression Test Suite (Standalone Backend)
add_executable(test_regression tests/regression_main.cpp)
target_link_libraries(test_regression PRIVATE acutesim_compute_core)
target_include_directories(test_regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_regression PUBLIC cxx_std_17)

if(EMSCRIPTEN)
    target_link_options(test_regression PRIVATE
        "--bind" 
        "-sWASM=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=1"
        "-sEXPORTED_RUNTIME_METHODS=['FS']"
    )
endif()


# AD Integration Test (Dual<T> vs Finite-Difference for all devices)
add_executable(test_ad_integration tests/test_ad_integration.cpp)
target_link_libraries(test_ad_integration PRIVATE acutesim_engine)
target_include_directories(test_ad_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(test_ad_integration PUBLIC cxx_std_17)

# Sensitivity Analysis Test (AD Gradient Check)
add_executable(test_sensitivity tests/test_sensitivity.cpp)
target_link_libraries(test_sensitivity PRIVATE acutesim_compute_core)
target_include_directories(test_sensitivity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_sensitivity PUBLIC cxx_std_17)

# Mode Ladder Regression Test
add_executable(test_mode_ladder tests/test_mode_ladder.cpp)
target_link_libraries(test_mode_ladder PRIVATE acutesim_compute_core)
target_include_directories(test_mode_ladder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_mode_ladder PUBLIC cxx_std_17)

# Virtuoso Parity Verification Test
if(NOT EMSCRIPTEN)
    add_executable(test_virtuoso_parity tests/test_virtuoso_parity.cpp)
    target_link_libraries(test_virtuoso_parity PRIVATE acutesim_orchestration Qt6::Gui Qt6::Widgets Qt6::Concurrent)
    target_include_directories(test_virtuoso_parity PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_features(test_virtuoso_parity PUBLIC cxx_std_17)
endif()

# Numerical Parity Verification Test (Phase D Hardening)
add_executable(test_numerical_parity tests/test_numerical_parity.cpp)
target_link_libraries(test_numerical_parity PRIVATE acutesim_compute_core)
target_include_directories(test_numerical_parity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(test_numerical_parity PUBLIC cxx_std_17)

# ===========================================================================
# Engine Parity & Isolation Test (Phase F — Engine Migration Verification)
# ===========================================================================
# CRITICAL: Links ONLY against acutesim_engine + engine_api.
# If this target builds, it proves:
#   - engine_api/ has no internal header leakage
#   - SessionImpl is correctly wired (6 analysis methods)
#   - ISimulationEngine::create() factory is reachable as ENGINE_API symbol
# Run: ctest -R test_engine_parity --verbose
if(NOT EMSCRIPTEN)
    add_executable(test_engine_parity tests/test_engine_parity.cpp)
    target_link_libraries(test_engine_parity PRIVATE acutesim_engine)
    target_include_directories(test_engine_parity PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/engine_api
    )
    target_compile_features(test_engine_parity PUBLIC cxx_std_17)
    add_test(NAME EngineParityTest COMMAND test_engine_parity)

    # Capability probe as the first GPU-touching call (AcuteSim main() order)
    add_executable(test_capabilities_probe tests/test_capabilities_probe.cpp)
    target_link_libraries(test_capabilities_probe PRIVATE acutesim_engine)
    add_test(NAME CapabilitiesProbeTest COMMAND test_capabilities_probe)

endif()

# Solver Tier Benchmarking Suite
# Compares all 5 solver tiers on reference circuits with wall-clock timing.
# Run: ./build/test_benchmark_solvers [--nodes 100] [--csv]
if(NOT EMSCRIPTEN)
    add_executable(test_benchmark_solvers
        tests/test_benchmark_solvers.cpp
    )
    target_link_libraries(test_benchmark_solvers PRIVATE acutesim_engine)
    target_include_directories(test_benchmark_solvers PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/engine_api
    )
    target_compile_features(test_benchmark_solvers PUBLIC cxx_std_17)
    # Note: test_benchmark_solvers is NOT registered with ctest; run directly.
endif()

# Demo: SPICE-style netlist → node voltages output
# Exercises the full CircuitSim::solveDC() pipeline end-to-end.
# Run: ./build/demo_spice_netlist
if(NOT EMSCRIPTEN)
    add_executable(demo_spice_netlist
        tests/demo_spice_netlist.cpp
    )
    target_link_libraries(demo_spice_netlist PRIVATE acutesim_engine)
    target_include_directories(demo_spice_netlist PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/engine_api
    )
    target_compile_features(demo_spice_netlist PUBLIC cxx_std_17)
endif()

# GPU Parity Verification Test (Phase D5c — requires Dawn native)
if(NOT EMSCRIPTEN)
    add_executable(test_gpu_parity
        tests/test_gpu_parity.cpp
    )
    target_include_directories(test_gpu_parity PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/components
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${DAWN_INTERNAL_INCLUDES}
    )
    target_link_libraries(test_gpu_parity PRIVATE
        acutesim_engine
        acutesim_compute_core
        dawn_native
        dawn_proc
        webgpu_cpp
    )
    target_compile_definitions(test_gpu_parity PRIVATE ACUTESIM_HAS_DAWN_NATIVE)
    target_compile_features(test_gpu_parity PUBLIC cxx_std_17)
    add_test(NAME test_gpu_parity COMMAND test_gpu_parity)
    set_tests_properties(test_gpu_parity PROPERTIES LABELS "gpu;heavy")
endif()

# Auto-Validation Harness Test (ERC + AI API)
if(NOT EMSCRIPTEN)
    add_executable(test_auto_harness
        tests/test_auto_harness.cpp
        gui/components/visual_component.cpp
        gui/components/probe_tip.cpp
        gui/core/hierarchy_path.cpp
        gui/widgets/breadcrumb_widget.cpp
        gui/editors/schematic/schematic_editor.cpp
        gui/editors/schematic/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic/schematic_editor_api.cpp
        gui/editors/schematic/schematic_editor_sync.cpp
        gui/panels/ai_assistant_panel.cpp
        gui/widgets/component_library.cpp
        gui/widgets/design_flow.cpp
        gui/analysis/system_inspector.cpp
        gui/analysis/logic_analyzer.cpp
        gui/editors/code_editor.cpp
        gui/editors/json_viewer.cpp
        gui/editors/image_viewer.cpp
        gui/editors/hex_viewer.cpp
        gui/core/editor_manager.cpp
        gui/core/editor_factory.cpp
        gui/core/sim_auto_detector.cpp
        gui/core/mainwindow.cpp
        gui/core/welcome_widget.cpp
        gui/components/standard_cell_visualizer.cpp
        gui/core/subcircuit_resolver.cpp
        components/design_db.cpp
        gui/analysis/pdk_manager.cpp
        gui/orchestration/pdk_manager.cpp
        gui/orchestration/pdk_browser_panel.cpp
    gui/scripting/tcl_bridge.cpp
    gui/analysis/pdk_compliance.cpp
    gui/widgets/compliance_report_widget.cpp
        components/netlist_export.cpp
        ai_api/system/schema_api.cpp
        ai_api/context/context_engine.cpp
        ai_api/analysis/failure_classifier.cpp
        ai_api/simulation/auto_harness.cpp
        ai_api/simulation/speculative_runner.cpp
        ai_api/context/prompt_builder.cpp
        ai_api/context/rag_context.cpp
        ai_api/analysis/topology_analyzer.cpp
        ai_api/system/feedback_loop.cpp
        ai_api/simulation/simulation_trace.cpp
        ai_api/context/context_budget.cpp
        ai_api/adk/tool_registry.cpp
        ai_api/adk/tool_handlers.cpp
        ai_api/adk/session_manager.cpp
        ai_api/pipeline/rag_pipeline.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_driver_adapter.h
        gui/widgets/engine_console.cpp
        gui/widgets/engine_log_panel.cpp
        gui/analysis/variables_panel.cpp
        gui/analysis/variables_panel.h
        gui/analysis/calculator_widget.cpp
        gui/analysis/calculator_widget.h
        gui/analysis/parametric_sweep_dialog.cpp
        gui/analysis/parametric_sweep_dialog.h

        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        gui/orchestration/rtl_pipeline_panel.cpp
        gui/orchestration/synthesis_result_dialog.cpp
        gui/orchestration/pnr_result_dialog.cpp
        gui/orchestration/signoff_result_dialog.cpp
        gui/editors/physical_overlay_item.cpp
        gui/widgets/cell_pie_chart.h
        gui/widgets/timing_indicator.h

        compute/orchestration/grpc_worker_client.cpp
        gui/analysis/statistical_dashboard.cpp
        gui/analysis/monte_carlo_setup_widget.cpp
        gui/styling/theme_manager.cpp
        gui/core/layout_manager.cpp
        gui/core/storage_manager.cpp
        gui/core/docking_manager.cpp
        gui/widgets/waveform_widget.cpp
        gui/widgets/output_dock_widget.cpp
        gui/widgets/terminal_widget.cpp
        gui/core/platform_helper.cpp
        gui/core/activity_bar.cpp
        gui/core/activity_bar.h
        gui/core/icon_provider.cpp
        gui/core/icon_provider.h
        resources/icons.qrc
        gui/widgets/command_palette.cpp
        gui/widgets/command_palette.h
        gui/widgets/ghost_diff_viewer.cpp
        gui/widgets/ghost_diff_viewer.h
        gui/widgets/toast_manager.cpp
        gui/widgets/toast_manager.h
        gui/core/keybinding_manager.cpp
        gui/core/keybinding_manager.h
        gui/widgets/keybindings_dialog.cpp
        gui/widgets/keybindings_dialog.h
        gui/widgets/inline_dialog_types.h
        gui/widgets/inline_dialog.cpp
        gui/widgets/inline_dialog.h
        gui/widgets/inline_progress.cpp
        gui/widgets/inline_progress.h
        gui/widgets/search_dock.cpp
        gui/widgets/search_dock.h
        gui/widgets/project_model.cpp
        gui/widgets/project_model.h
        gui/widgets/project_explorer.cpp
        gui/widgets/project_explorer.h
        gui/widgets/library_model.cpp
        gui/widgets/library_model.h
        gui/orchestration/library_index.cpp
        gui/orchestration/library_index.h
    )
    target_include_directories(test_auto_harness PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/analysis
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/components
        ${CMAKE_CURRENT_SOURCE_DIR}/physics
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
        ${CMAKE_CURRENT_SOURCE_DIR}/components
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/adk
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/pipeline
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
    )
    target_link_libraries(test_auto_harness PRIVATE acutesim_orchestration Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent ${TCL_LIBRARY})
    target_compile_features(test_auto_harness PUBLIC cxx_std_17)
endif()

# Copilot Integration Test
if(NOT EMSCRIPTEN)
    add_executable(test_copilot_validate
        tests/test_copilot_validate.cpp
        gui/components/visual_component.cpp
        gui/components/probe_tip.cpp
        gui/core/hierarchy_path.cpp
        gui/widgets/breadcrumb_widget.cpp
        gui/editors/schematic/schematic_editor.cpp
        gui/editors/schematic/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic/schematic_editor_api.cpp
        gui/editors/schematic/schematic_editor_sync.cpp
        gui/panels/ai_assistant_panel.cpp
        gui/widgets/component_library.cpp
        gui/widgets/design_flow.cpp
        gui/analysis/system_inspector.cpp
        gui/analysis/logic_analyzer.cpp
        gui/editors/code_editor.cpp
        gui/editors/json_viewer.cpp
        gui/editors/image_viewer.cpp
        gui/editors/hex_viewer.cpp
        gui/core/editor_manager.cpp
        gui/core/editor_factory.cpp
        gui/core/sim_auto_detector.cpp
        gui/core/mainwindow.cpp
        gui/core/subcircuit_resolver.cpp
        gui/components/standard_cell_visualizer.cpp
    gui/scripting/tcl_bridge.cpp
    gui/analysis/pdk_compliance.cpp
    gui/widgets/compliance_report_widget.cpp
        gui/core/welcome_widget.cpp
        components/design_db.cpp
        gui/analysis/pdk_manager.cpp
        gui/orchestration/pdk_manager.cpp
        gui/orchestration/pdk_browser_panel.cpp
        components/netlist_export.cpp
        ai_api/system/schema_api.cpp
        ai_api/context/context_engine.cpp
        ai_api/analysis/failure_classifier.cpp
        ai_api/simulation/auto_harness.cpp
        ai_api/simulation/speculative_runner.cpp
        ai_api/context/prompt_builder.cpp
        ai_api/context/rag_context.cpp
        ai_api/analysis/topology_analyzer.cpp
        ai_api/system/feedback_loop.cpp
        ai_api/simulation/simulation_trace.cpp
        ai_api/context/context_budget.cpp
        ai_api/adk/tool_registry.cpp
        ai_api/adk/tool_handlers.cpp
        ai_api/adk/session_manager.cpp
        ai_api/pipeline/rag_pipeline.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_driver_adapter.h
        gui/widgets/engine_console.cpp
        gui/widgets/engine_log_panel.cpp
        gui/analysis/variables_panel.cpp
        gui/analysis/variables_panel.h
        gui/analysis/calculator_widget.cpp
        gui/analysis/calculator_widget.h
        gui/analysis/parametric_sweep_dialog.cpp
        gui/analysis/parametric_sweep_dialog.h

        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        gui/orchestration/rtl_pipeline_panel.cpp
        gui/orchestration/synthesis_result_dialog.cpp
        gui/orchestration/pnr_result_dialog.cpp
        gui/orchestration/signoff_result_dialog.cpp
        gui/editors/physical_overlay_item.cpp
        gui/widgets/cell_pie_chart.h
        gui/widgets/timing_indicator.h


        compute/orchestration/grpc_worker_client.cpp
        gui/analysis/statistical_dashboard.cpp
        gui/analysis/monte_carlo_setup_widget.cpp
        gui/styling/theme_manager.cpp
        gui/core/layout_manager.cpp
        gui/core/storage_manager.cpp
        gui/core/docking_manager.cpp
        gui/widgets/waveform_widget.cpp
        gui/widgets/output_dock_widget.cpp
        gui/widgets/terminal_widget.cpp
        gui/core/platform_helper.cpp
        gui/core/activity_bar.cpp
        gui/core/activity_bar.h
        gui/core/icon_provider.cpp
        gui/core/icon_provider.h
        resources/icons.qrc
        gui/widgets/command_palette.cpp
        gui/widgets/command_palette.h
        gui/widgets/ghost_diff_viewer.cpp
        gui/widgets/ghost_diff_viewer.h
        gui/widgets/toast_manager.cpp
        gui/widgets/toast_manager.h
        gui/core/keybinding_manager.cpp
        gui/core/keybinding_manager.h
        gui/widgets/keybindings_dialog.cpp
        gui/widgets/keybindings_dialog.h
        gui/widgets/inline_dialog_types.h
        gui/widgets/inline_dialog.cpp
        gui/widgets/inline_dialog.h
        gui/widgets/inline_progress.cpp
        gui/widgets/inline_progress.h
        gui/widgets/search_dock.cpp
        gui/widgets/search_dock.h
        gui/widgets/project_model.cpp
        gui/widgets/project_model.h
        gui/widgets/project_explorer.cpp
        gui/widgets/project_explorer.h
        gui/widgets/library_model.cpp
        gui/widgets/library_model.h
        gui/orchestration/library_index.cpp
        gui/orchestration/library_index.h
    )
    target_include_directories(test_copilot_validate PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/analysis
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/components
        ${CMAKE_CURRENT_SOURCE_DIR}/physics
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
        ${CMAKE_CURRENT_SOURCE_DIR}/components
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/adk
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/pipeline
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
    )
    target_link_libraries(test_copilot_validate PRIVATE acutesim_orchestration Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent ${TCL_LIBRARY})
    target_compile_features(test_copilot_validate PUBLIC cxx_std_17)
endif()

# ERC Rules Automated Test
add_executable(test_erc_rules tests/test_erc_rules.cpp)
target_link_libraries(test_erc_rules PRIVATE acutesim_compute_core)
target_include_directories(test_erc_rules PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_erc_rules PUBLIC cxx_std_17)

# Phase 1.1 Statistical Interactivity Test
if(NOT EMSCRIPTEN)
    add_executable(test_statistical_interactivity
        tests/test_statistical_interactivity.cpp
        gui/analysis/statistical_dashboard.cpp
        gui/analysis/statistical_dashboard.h
    )
    target_include_directories(test_statistical_interactivity PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/analysis
        ${CMAKE_CURRENT_SOURCE_DIR}/physics
        ${CMAKE_CURRENT_SOURCE_DIR}/components
    )
    target_link_libraries(test_statistical_interactivity PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
    target_compile_features(test_statistical_interactivity PUBLIC cxx_std_17)
endif()

# Phase 1.26 AI Simulation Intelligence Test
if(NOT EMSCRIPTEN)
    add_executable(test_ai_simulation_intelligence
        tests/test_ai_simulation_intelligence.cpp
        ai_api/simulation/simulation_trace.cpp
        ai_api/simulation/speculative_runner.cpp
        ai_api/analysis/failure_classifier.cpp
    )
    target_include_directories(test_ai_simulation_intelligence PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
    )
    target_link_libraries(test_ai_simulation_intelligence PRIVATE acutesim_compute_core Qt6::Test Qt6::Concurrent)
    target_compile_features(test_ai_simulation_intelligence PUBLIC cxx_std_17)
endif()

# Phase 2 I/O Safety Tests
add_executable(test_safe_io tests/test_safe_io.cpp)
target_include_directories(test_safe_io PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
)
target_link_libraries(test_safe_io PRIVATE Qt6::Core Qt6::Test)
target_compile_features(test_safe_io PUBLIC cxx_std_17)

# Phase 1.26: Context Budget Tests
add_executable(test_context_budget
    tests/test_context_budget.cpp
    ai_api/context/context_budget.cpp
)
target_include_directories(test_context_budget PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
)
target_link_libraries(test_context_budget PRIVATE Qt6::Core Qt6::Test)
target_compile_features(test_context_budget PUBLIC cxx_std_17)

# Phase 1.26: ADK Tool Registry Tests
add_executable(test_adk_tool_registry
    tests/test_adk_tool_registry.cpp
    tests/stubs/ai_api_test_stubs.cpp
    ai_api/adk/tool_registry.cpp
    ai_api/adk/tool_handlers.cpp
    ai_api/adk/session_manager.cpp
    ai_api/context/context_budget.cpp
    ai_api/context/rag_context.cpp
    ai_api/context/prompt_builder.cpp
    ai_api/system/feedback_loop.cpp
    ai_api/simulation/simulation_trace.cpp
    ai_api/analysis/failure_classifier.cpp
    ai_api/analysis/topology_analyzer.cpp
    ai_api/pipeline/rag_pipeline.cpp
)
target_include_directories(test_adk_tool_registry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/adk
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
)
target_include_directories(test_adk_tool_registry PRIVATE $<TARGET_PROPERTY:Qt6::Gui,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(test_adk_tool_registry PRIVATE Qt6::Core Qt6::Test)
target_compile_features(test_adk_tool_registry PUBLIC cxx_std_17)

# Phase 1.26: RAG Pipeline Tests
add_executable(test_rag_pipeline
    tests/test_rag_pipeline.cpp
    tests/stubs/ai_api_test_stubs.cpp
    ai_api/pipeline/rag_pipeline.cpp
    ai_api/context/context_budget.cpp
    ai_api/context/rag_context.cpp
    ai_api/context/prompt_builder.cpp
    ai_api/system/feedback_loop.cpp
    ai_api/simulation/simulation_trace.cpp
    ai_api/analysis/failure_classifier.cpp
    ai_api/analysis/topology_analyzer.cpp
)
target_include_directories(test_rag_pipeline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/adk
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
)
target_include_directories(test_rag_pipeline PRIVATE $<TARGET_PROPERTY:Qt6::Gui,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(test_rag_pipeline PRIVATE Qt6::Core Qt6::Test)
target_compile_features(test_rag_pipeline PUBLIC cxx_std_17)

# Phase 1.26: Session Manager Tests
add_executable(test_session_manager
    tests/test_session_manager.cpp
    ai_api/adk/session_manager.cpp
)
target_include_directories(test_session_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/adk
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
)
target_link_libraries(test_session_manager PRIVATE Qt6::Core Qt6::Test)
target_compile_features(test_session_manager PUBLIC cxx_std_17)

# Phase 1.7 Simulation Suite Tests
add_executable(test_simulation tests/simulation_test_main.cpp)
target_link_libraries(test_simulation PRIVATE acutesim_compute_core Qt6::Core)
target_include_directories(test_simulation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_simulation PUBLIC cxx_std_17)

# BJT Thermal Verification
add_executable(verify_bjt_thermal tests/verify_bjt_thermal.cpp)
target_link_libraries(verify_bjt_thermal PRIVATE acutesim_compute_core)
target_include_directories(verify_bjt_thermal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(verify_bjt_thermal PUBLIC cxx_std_17)

# Dependent Source Test
add_executable(test_dependent_simulation tests/test_dependent_simulation.cpp)
target_link_libraries(test_dependent_simulation PRIVATE acutesim_compute_core)
target_include_directories(test_dependent_simulation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_dependent_simulation PUBLIC cxx_std_17)

# Test PDK Manager
add_executable(test_pdk_manager tests/test_pdk_manager.cpp gui/analysis/pdk_manager.cpp)
target_include_directories(test_pdk_manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/analysis
)
target_link_libraries(test_pdk_manager PRIVATE Qt6::Core Qt6::Test)
target_link_libraries(test_pdk_manager PRIVATE Qt6::Core Qt6::Test)
target_compile_features(test_pdk_manager PUBLIC cxx_std_17)

# Test PDK Swap (Regression)
add_executable(test_pdk_swap
    tests/test_pdk_swap.cpp
    gui/analysis/pdk_manager.cpp
    gui/analysis/pdk_compliance.cpp
    components/netlist_export.cpp

    gui/components/visual_component.cpp

    gui/components/probe_tip.cpp
    gui/styling/theme_manager.cpp
    gui/styling/theme_manager.h
    gui/core/hierarchy_path.cpp
    gui/widgets/breadcrumb_widget.cpp
    tests/stubs/grid_scene_stub.cpp # Stub implementation
    components/design_db.cpp
    gui/editors/schematic/schematic_editor.h # Triggers MOC for GridScene
    gui/core/editor_manager.cpp
    gui/core/editor_factory.cpp
    gui/core/storage_manager.cpp
    gui/core/sim_auto_detector.cpp
    gui/editors/code_editor.cpp
    gui/editors/json_viewer.cpp
    gui/editors/image_viewer.cpp
    gui/editors/hex_viewer.cpp
    gui/core/icon_provider.cpp
    gui/orchestration/worker_manager.cpp
    gui/orchestration/pdk_manager.cpp
    gui/orchestration/pdk_browser_panel.cpp
    compute/orchestration/grpc_worker_client.cpp
    gui/widgets/inline_dialog.cpp
    gui/widgets/inline_progress.cpp
    gui/widgets/toast_manager.cpp
    compute/orchestration/drivers/yosys_log_parser.cpp
    compute/orchestration/drivers/openroad_report_parser.cpp
    compute/orchestration/drivers/klayout_report_parser.cpp
    compute/orchestration/server/subprocess.cpp
    compute/orchestration/drivers/rtl_flow.cpp
    gui/widgets/cell_pie_chart.h
    gui/widgets/timing_indicator.h
)
target_include_directories(test_pdk_swap PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
)
target_link_libraries(test_pdk_swap PRIVATE acutesim_compute_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_compile_features(test_pdk_swap PUBLIC cxx_std_17)

# Emscripten link options for all test targets
if(EMSCRIPTEN)
    set(EMSCRIPTEN_TEST_LINK_OPTIONS
        "--bind" 
        "-sWASM=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=1"
        "-sEXPORTED_RUNTIME_METHODS=['FS']"
    )
    target_link_options(test_simulation PRIVATE ${EMSCRIPTEN_TEST_LINK_OPTIONS})
    target_link_options(test_sensitivity PRIVATE ${EMSCRIPTEN_TEST_LINK_OPTIONS})
    target_link_options(test_erc_rules PRIVATE ${EMSCRIPTEN_TEST_LINK_OPTIONS})
    # test_auto_harness and test_copilot_validate excluded from WASM due to threading
    target_link_options(test_erc_rules PRIVATE ${EMSCRIPTEN_TEST_LINK_OPTIONS})
    # test_auto_harness and test_copilot_validate excluded from WASM due to threading
endif()

# Phase 1.5 PDK Compliance Test (GUI Integration)
if(NOT EMSCRIPTEN)
    add_executable(test_pdk_compliance
        tests/test_pdk_compliance.cpp
        gui/components/visual_component.cpp
        gui/components/probe_tip.cpp
        gui/core/hierarchy_path.cpp
        gui/widgets/breadcrumb_widget.cpp
        gui/editors/schematic/schematic_editor.cpp
        gui/editors/schematic/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic/schematic_editor_api.cpp
        gui/editors/schematic/schematic_editor_sync.cpp
        gui/panels/ai_assistant_panel.cpp
        gui/widgets/component_library.cpp
        gui/widgets/design_flow.cpp
        gui/analysis/system_inspector.cpp
        gui/editors/code_editor.cpp
        gui/editors/json_viewer.cpp
        gui/editors/image_viewer.cpp
        gui/editors/hex_viewer.cpp
        gui/core/editor_manager.cpp
        gui/core/editor_factory.cpp
        gui/core/sim_auto_detector.cpp
        gui/core/mainwindow.cpp
        gui/core/welcome_widget.cpp
        gui/components/standard_cell_visualizer.cpp
        gui/core/subcircuit_resolver.cpp
        components/design_db.cpp
        gui/analysis/pdk_manager.cpp
        gui/orchestration/pdk_manager.cpp
        gui/orchestration/pdk_browser_panel.cpp
        components/netlist_export.cpp
        ai_api/system/schema_api.cpp
        ai_api/context/context_engine.cpp
        ai_api/analysis/failure_classifier.cpp
        ai_api/simulation/auto_harness.cpp
        ai_api/simulation/speculative_runner.cpp
        ai_api/context/prompt_builder.cpp
        ai_api/context/rag_context.cpp
        ai_api/analysis/topology_analyzer.cpp
        ai_api/system/feedback_loop.cpp
        ai_api/simulation/simulation_trace.cpp
        ai_api/context/context_budget.cpp
        ai_api/adk/tool_registry.cpp
        ai_api/adk/tool_handlers.cpp
        ai_api/adk/session_manager.cpp
        ai_api/pipeline/rag_pipeline.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_driver_adapter.h
        gui/analysis/variables_panel.cpp
        gui/analysis/variables_panel.h
        gui/analysis/calculator_widget.cpp
        gui/analysis/calculator_widget.h
        gui/analysis/parametric_sweep_dialog.cpp
        gui/analysis/parametric_sweep_dialog.h

        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/engine_console.cpp
        gui/widgets/engine_log_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        gui/orchestration/rtl_pipeline_panel.cpp
        gui/orchestration/synthesis_result_dialog.cpp
        gui/orchestration/pnr_result_dialog.cpp
        gui/orchestration/signoff_result_dialog.cpp
        gui/editors/physical_overlay_item.cpp
        gui/widgets/cell_pie_chart.h
        gui/widgets/timing_indicator.h

        compute/orchestration/grpc_worker_client.cpp
        gui/analysis/statistical_dashboard.cpp
        gui/analysis/monte_carlo_setup_widget.cpp
        gui/styling/theme_manager.cpp
        gui/core/layout_manager.cpp
        gui/core/storage_manager.cpp
        gui/core/docking_manager.cpp
        gui/widgets/waveform_widget.cpp
        gui/widgets/output_dock_widget.cpp
        gui/widgets/terminal_widget.cpp
        gui/core/platform_helper.cpp
        gui/core/activity_bar.cpp
        gui/core/icon_provider.cpp
        resources/icons.qrc
        gui/widgets/command_palette.cpp
        gui/widgets/ghost_diff_viewer.cpp
        gui/widgets/toast_manager.cpp
        gui/core/keybinding_manager.cpp
        gui/widgets/keybindings_dialog.cpp
        gui/widgets/inline_dialog.cpp
        gui/widgets/inline_progress.cpp
        gui/widgets/search_dock.cpp
        gui/widgets/project_model.cpp
        gui/widgets/project_explorer.cpp
        gui/widgets/project_explorer.h
        gui/widgets/library_model.cpp
        gui/widgets/library_model.h
        gui/orchestration/library_index.cpp
        gui/orchestration/library_index.h
        gui/analysis/pdk_compliance.cpp
        gui/analysis/pdk_manager.cpp
        components/design_db.cpp
        gui/scripting/tcl_bridge.cpp
        gui/widgets/compliance_report_widget.cpp
    )
    target_include_directories(test_pdk_compliance PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/analysis
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/components
        ${CMAKE_CURRENT_SOURCE_DIR}/physics
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
        ${CMAKE_CURRENT_SOURCE_DIR}/components
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/adk
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/pipeline
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
        ${TCL_INCLUDE_PATH}
    )
    target_link_libraries(test_pdk_compliance PRIVATE acutesim_orchestration Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent ${TCL_LIBRARY})
    target_compile_features(test_pdk_compliance PUBLIC cxx_std_17)
endif()

# =============================================================================
# Testing Configuration
# =============================================================================
# Phase 2 Hierarchical Sim Test
if(NOT EMSCRIPTEN)
    add_executable(test_hierarchical_sim
        tests/test_hierarchical_sim.cpp
        gui/components/visual_component.cpp
        gui/components/probe_tip.cpp
        gui/core/hierarchy_path.cpp
        gui/widgets/breadcrumb_widget.cpp
        gui/editors/schematic/schematic_editor.cpp
        gui/editors/schematic/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic/schematic_editor_api.cpp
        gui/editors/schematic/schematic_editor_sync.cpp
        gui/panels/ai_assistant_panel.cpp
        gui/widgets/component_library.cpp
        gui/widgets/design_flow.cpp
        gui/analysis/system_inspector.cpp
        gui/analysis/logic_analyzer.cpp
        gui/editors/code_editor.cpp
        gui/core/editor_manager.cpp
        gui/core/sim_auto_detector.cpp
        gui/components/standard_cell_visualizer.cpp
        gui/core/subcircuit_resolver.cpp
        gui/core/mainwindow.cpp
        gui/core/welcome_widget.cpp
        ai_api/system/schema_api.cpp
        ai_api/context/context_engine.cpp
        ai_api/analysis/failure_classifier.cpp
        ai_api/simulation/auto_harness.cpp
        ai_api/simulation/speculative_runner.cpp
        ai_api/context/prompt_builder.cpp
        ai_api/context/rag_context.cpp
        ai_api/analysis/topology_analyzer.cpp
        ai_api/system/feedback_loop.cpp
        ai_api/simulation/simulation_trace.cpp
        ai_api/context/context_budget.cpp
        ai_api/adk/tool_registry.cpp
        ai_api/adk/tool_handlers.cpp
        ai_api/adk/session_manager.cpp
        ai_api/pipeline/rag_pipeline.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_driver_adapter.h
        gui/widgets/engine_console.cpp
        gui/widgets/engine_log_panel.cpp
        gui/analysis/variables_panel.cpp
        gui/analysis/variables_panel.h
        gui/analysis/calculator_widget.cpp
        gui/analysis/calculator_widget.h
        gui/analysis/parametric_sweep_dialog.cpp
        gui/analysis/parametric_sweep_dialog.h

        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        gui/orchestration/rtl_pipeline_panel.cpp
        gui/orchestration/synthesis_result_dialog.cpp
        gui/orchestration/pnr_result_dialog.cpp
        gui/orchestration/signoff_result_dialog.cpp
        gui/editors/physical_overlay_item.cpp
        gui/widgets/cell_pie_chart.h
        gui/widgets/timing_indicator.h

        compute/orchestration/grpc_worker_client.cpp
        gui/analysis/statistical_dashboard.cpp
        gui/analysis/monte_carlo_setup_widget.cpp
        gui/styling/theme_manager.cpp
        gui/core/layout_manager.cpp
        gui/core/storage_manager.cpp
        gui/core/docking_manager.cpp
        gui/widgets/waveform_widget.cpp
        gui/widgets/output_dock_widget.cpp
        gui/widgets/terminal_widget.cpp
        gui/core/platform_helper.cpp
        gui/core/activity_bar.cpp
        gui/core/icon_provider.cpp
        resources/icons.qrc
        gui/analysis/pdk_compliance.cpp
        gui/widgets/compliance_report_widget.cpp
        gui/widgets/command_palette.cpp
        gui/widgets/ghost_diff_viewer.cpp
        gui/widgets/toast_manager.cpp
        gui/core/keybinding_manager.cpp
        gui/widgets/keybindings_dialog.cpp
        gui/widgets/inline_dialog.cpp
        gui/widgets/inline_progress.cpp
        gui/widgets/search_dock.cpp
        gui/widgets/project_model.cpp
        gui/widgets/project_explorer.cpp
        gui/widgets/project_explorer.h
        gui/widgets/library_model.cpp
        gui/widgets/library_model.h
        gui/orchestration/library_index.cpp
        gui/orchestration/library_index.h
        components/design_db.cpp
        gui/analysis/pdk_manager.cpp
        components/netlist_export.cpp
        gui/scripting/tcl_bridge.cpp
        gui/editors/json_viewer.cpp
        gui/editors/image_viewer.cpp
        gui/editors/hex_viewer.cpp
        gui/core/editor_factory.cpp
        gui/orchestration/pdk_manager.cpp
        gui/orchestration/pdk_browser_panel.cpp
    )
    target_include_directories(test_hierarchical_sim PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/analysis
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/components
        ${CMAKE_CURRENT_SOURCE_DIR}/physics
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
        ${CMAKE_CURRENT_SOURCE_DIR}/components
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/adk
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/pipeline
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
    )
    target_link_libraries(test_hierarchical_sim PRIVATE acutesim_orchestration Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent ${TCL_LIBRARY})
    target_compile_features(test_hierarchical_sim PUBLIC cxx_std_17)
    target_compile_definitions(test_hierarchical_sim PRIVATE CURRENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME test_hierarchical_sim COMMAND test_hierarchical_sim)
endif()

# Phase 5: Hierarchy Path Unit Test
add_executable(test_hierarchy_path
    tests/test_hierarchy_path.cpp
    gui/core/hierarchy_path.cpp
    gui/core/hierarchy_path.h
)
target_include_directories(test_hierarchy_path PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
)
target_link_libraries(test_hierarchy_path PRIVATE Qt6::Core Qt6::Test)
target_compile_features(test_hierarchy_path PUBLIC cxx_std_17)
add_test(NAME test_hierarchy_path COMMAND test_hierarchy_path)

enable_testing()

add_test(NAME test_safe_io COMMAND test_safe_io)
add_test(NAME test_ai_simulation_intelligence COMMAND test_ai_simulation_intelligence)
add_test(NAME test_context_budget COMMAND test_context_budget)
add_test(NAME test_adk_tool_registry COMMAND test_adk_tool_registry)
add_test(NAME test_rag_pipeline COMMAND test_rag_pipeline)
add_test(NAME test_session_manager COMMAND test_session_manager)

# Helper for testing Tcl without GUI
# add_executable(test_tcl_bridge tests/test_tcl_bridge.cpp gui/scripting/tcl_bridge.cpp components/design_db.cpp)
# target_link_libraries(test_tcl_bridge PRIVATE TCL_LIBRARY)

# Spatial Logic Test (Headless-ish GUI)
if(NOT EMSCRIPTEN)
    add_executable(test_spatial_logic tests/test_spatial_logic.cpp)
    target_link_libraries(test_spatial_logic PRIVATE acutesim_compute_core Qt6::Widgets Qt6::Core Qt6::Gui ${TCL_LIBRARY})
    # Link against app objects or sources?
    # Currently CuteApp sources are scattered. Best to add them or link a library if available.
    # Since we don't have a library target for gui/components, we must add sources interactively.
    target_sources(test_spatial_logic PRIVATE
        gui/editors/schematic/schematic_editor.cpp
        gui/editors/schematic/schematic_editor_api.cpp
        gui/editors/schematic/schematic_editor_sync.cpp
        gui/editors/schematic/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/components/visual_component.cpp
        gui/components/probe_tip.cpp
        gui/styling/theme_manager.cpp
        gui/core/editor_manager.cpp
        gui/core/editor_factory.cpp
        gui/core/storage_manager.cpp
        gui/core/sim_auto_detector.cpp
        gui/core/hierarchy_path.cpp
        gui/widgets/breadcrumb_widget.cpp
        gui/editors/code_editor.cpp
        gui/editors/json_viewer.cpp
        gui/editors/image_viewer.cpp
        gui/editors/hex_viewer.cpp
        gui/core/icon_provider.cpp
        gui/widgets/compliance_report_widget.cpp
        gui/components/standard_cell_visualizer.cpp
        gui/core/subcircuit_resolver.cpp
        components/design_db.cpp
        gui/analysis/pdk_manager.cpp
        gui/orchestration/library_index.cpp
        components/netlist_export.cpp
        gui/analysis/pdk_compliance.cpp
        gui/scripting/tcl_bridge.cpp
        gui/orchestration/worker_manager.cpp
        gui/orchestration/pdk_manager.cpp
        gui/orchestration/pdk_browser_panel.cpp
        gui/orchestration/synthesis_result_dialog.cpp
        gui/orchestration/pnr_result_dialog.cpp
        gui/orchestration/signoff_result_dialog.cpp
        gui/editors/physical_overlay_item.cpp
        gui/widgets/inline_dialog.cpp
        gui/widgets/inline_progress.cpp
        gui/widgets/toast_manager.cpp
        compute/orchestration/server/subprocess.cpp
        compute/orchestration/drivers/rtl_flow.cpp
        compute/orchestration/drivers/yosys_log_parser.cpp
        compute/orchestration/drivers/openroad_report_parser.cpp
        compute/orchestration/drivers/klayout_report_parser.cpp
        gui/widgets/cell_pie_chart.h
        gui/widgets/timing_indicator.h
        compute/orchestration/grpc_worker_client.cpp
    )


    # Includes
    target_include_directories(test_spatial_logic PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/components
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
        ${CMAKE_CURRENT_SOURCE_DIR}/components
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/analysis
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/scripting
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/orchestration
    )

endif()

# Power Domain Test (Phase 1.3)
if(NOT EMSCRIPTEN)
    add_executable(test_power_domains tests/test_power_domains.cpp)
    target_link_libraries(test_power_domains PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui)
    target_sources(test_power_domains PRIVATE
        components/design_db.cpp
    )
    target_include_directories(test_power_domains PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/components
    )
endif()

# Phase 5 GUI Logic Test
add_executable(test_gui_logic tests/test_gui_logic.cpp gui/components/visual_component.cpp gui/components/probe_tip.cpp gui/styling/theme_manager.cpp gui/core/platform_helper.cpp)
target_include_directories(test_gui_logic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_gui_logic PRIVATE acutesim_compute_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
# visual_component.cpp needs these to link if it references SchemeEditor or similar? 
# VisualComponent only depends on Qt Graphics, so simple link should work unless circular deps exist.
add_test(NAME test_gui_logic COMMAND test_gui_logic)

# Phase 11.2 Docking Interaction Test
add_executable(test_docking_interaction
    tests/test_docking_interaction.cpp
    gui/core/layout_manager.cpp
    gui/widgets/terminal_widget.cpp
    gui/widgets/output_dock_widget.cpp
    gui/widgets/waveform_widget.cpp
    gui/styling/theme_manager.cpp
    gui/core/platform_helper.cpp
)
target_include_directories(test_docking_interaction PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/styling
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Netlist Export Tests
add_executable(test_verilog_export tests/test_verilog_export.cpp components/netlist_export.cpp)
target_link_libraries(test_verilog_export PRIVATE Qt6::Core) # Utils only
target_include_directories(test_verilog_export PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_spef_export tests/test_spef_export.cpp components/netlist_export.cpp)
target_link_libraries(test_spef_export PRIVATE Qt6::Core)
target_include_directories(test_spef_export PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_export_consistency tests/test_export_consistency.cpp components/netlist_export.cpp)
target_link_libraries(test_export_consistency PRIVATE Qt6::Core)
target_include_directories(test_export_consistency PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_multi_domain_power tests/test_multi_domain_power.cpp components/netlist_export.cpp)
target_link_libraries(test_multi_domain_power PRIVATE Qt6::Core)
target_include_directories(test_multi_domain_power PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_docking_interaction PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME test_docking_interaction COMMAND test_docking_interaction)


# Physics Kernels Verification (BJT, MOSFET, Diode)
add_executable(test_physics_kernels
    tests/test_physics_kernels.cpp
)
# Note: Solver sources linked only if dependencies require, but kernels are header-only effectively.
# Actually, device_physics.h is header only. We might not need solver cpp files if we don't link them.
# Removing solver cpps to keep it a clean unit test of physics.
target_sources(test_physics_kernels PRIVATE tests/test_physics_kernels.cpp)

target_include_directories(test_physics_kernels PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_physics_kernels PRIVATE Qt6::Core Qt6::Test)
target_compile_features(test_physics_kernels PUBLIC cxx_std_17)

add_test(NAME test_physics_kernels COMMAND test_physics_kernels)


# Emulated F64 Verification
add_executable(test_emulated_f64
    tests/test_emulated_f64.cpp
)
target_include_directories(test_emulated_f64 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_emulated_f64 PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_emulated_f64 COMMAND test_emulated_f64)


# AutoDiff Dual<EmulatedF64> Verification
add_executable(test_dual_emulated
    tests/test_dual_emulated.cpp
)
target_include_directories(test_dual_emulated PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/autodiff
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_dual_emulated PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_dual_emulated COMMAND test_dual_emulated)

# Test FPU Determinism
add_executable(test_fpu_determinism tests/test_fpu_determinism.cpp)
target_include_directories(test_fpu_determinism PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
)
target_link_libraries(test_fpu_determinism PRIVATE Qt6::Core)
add_test(NAME test_fpu_determinism COMMAND test_fpu_determinism)

# Determinism Replay Test
add_executable(test_determinism tests/test_determinism.cpp)
target_link_libraries(test_determinism PRIVATE acutesim_compute_core)
target_include_directories(test_determinism PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_determinism PUBLIC cxx_std_17)
add_test(NAME test_determinism COMMAND test_determinism)

# B2: Determinism Cross-Test
add_executable(test_determinism_xclient tests/test_determinism_xclient.cpp)
target_link_libraries(test_determinism_xclient PRIVATE acutesim_orchestration acutesim_compute_core Qt6::Core)
target_include_directories(test_determinism_xclient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(test_determinism_xclient PUBLIC cxx_std_17)
add_test(NAME test_determinism_xclient COMMAND test_determinism_xclient)
add_executable(test_driver_swap tests/test_driver_swap.cpp)
target_link_libraries(test_driver_swap PRIVATE acutesim_orchestration)
target_include_directories(test_driver_swap PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(test_driver_swap PUBLIC cxx_std_17)
add_test(NAME test_driver_swap COMMAND test_driver_swap)

# Hierarchy headless unit test (no Qt::Gui — avoids dual-Qt runtime crash)
# Covers: Pin::Direction::Power, TensorBlock::InterfacePin, CompiledInterfacePin,
#         NetUUID round-trip, DesignDb snapshot, NetlistCompiler propagation.
add_executable(test_hierarchy_headless
    tests/test_hierarchy_headless.cpp
    components/design_db.cpp
)
target_link_libraries(test_hierarchy_headless PRIVATE acutesim_compute_core)
target_compile_features(test_hierarchy_headless PUBLIC cxx_std_17)
add_test(NAME test_hierarchy_headless COMMAND test_hierarchy_headless)
set_tests_properties(test_hierarchy_headless PROPERTIES LABELS "quick")

# Phase 2.9: Numerical Intelligence Tests
add_executable(test_numerical_intelligence tests/test_numerical_intelligence.cpp)
target_link_libraries(test_numerical_intelligence PRIVATE acutesim_compute_core)
target_compile_features(test_numerical_intelligence PUBLIC cxx_std_17)
add_test(NAME test_numerical_intelligence COMMAND test_numerical_intelligence)
set_tests_properties(test_numerical_intelligence PROPERTIES LABELS "quick")

# Verification Harness (Benchmarks, Alignment, Dense Batch)
add_executable(test_verification_harness tests/verification_main.cpp)
target_link_libraries(test_verification_harness PRIVATE acutesim_compute_core)
target_include_directories(test_verification_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_verification_harness PUBLIC cxx_std_17)
# Standard Cell Verification (Generic Darlington)
# Reference SoC (Roadmap Phase 5): RV32I ISS, memory map, SPICE-characterised SRAM, UART co-simulation
if(NOT EMSCRIPTEN)
    add_library(acutesim_soc STATIC compute/soc/rv32i.cpp compute/soc/soc.cpp)
    target_link_libraries(acutesim_soc PUBLIC acutesim_engine)
    target_include_directories(acutesim_soc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(acutesim_soc PUBLIC cxx_std_17)
    add_executable(test_soc_hello tests/test_soc_hello.cpp)
    target_link_libraries(test_soc_hello PRIVATE acutesim_soc)
    target_compile_definitions(test_soc_hello PRIVATE SOC_DATASETS="${CMAKE_CURRENT_SOURCE_DIR}/datasets")
endif()

add_executable(test_std_cells
    tests/test_std_cells.cpp
    components/standard_cells.cpp
)
target_link_libraries(test_std_cells PRIVATE acutesim_compute_core Qt6::Core)
target_include_directories(test_std_cells PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_std_cells PUBLIC cxx_std_17)

if(NOT EMSCRIPTEN)
    # Validating flow tests
    add_executable(test_flow tests/test_flow.cpp)
    target_link_libraries(test_flow PRIVATE acutesim_engine Qt6::Core)
    target_include_directories(test_flow PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/components 
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Validating introspection tests
    add_executable(test_introspection
        tests/test_introspection.cpp
        gui/components/visual_component.cpp
        gui/styling/theme_manager.cpp
        gui/core/platform_helper.cpp
        gui/components/probe_tip.cpp
        gui/core/hierarchy_path.cpp
        gui/widgets/breadcrumb_widget.cpp
        gui/editors/schematic/schematic_editor.cpp
        gui/editors/schematic/schematic_editor_api.cpp
        gui/core/editor_manager.cpp
        gui/components/visual_component.cpp
        gui/components/probe_tip.cpp
        gui/editors/schematic/schematic_editor.cpp
        gui/editors/schematic/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic/schematic_editor_api.cpp
        gui/editors/schematic/schematic_editor_sync.cpp
        gui/panels/ai_assistant_panel.cpp
        gui/widgets/component_library.cpp
        gui/widgets/design_flow.cpp
        gui/analysis/system_inspector.cpp
        gui/analysis/logic_analyzer.cpp
        gui/editors/code_editor.cpp
        gui/core/editor_manager.cpp
        gui/core/sim_auto_detector.cpp
        gui/components/standard_cell_visualizer.cpp
        gui/core/subcircuit_resolver.cpp
        gui/core/mainwindow.cpp
        gui/core/welcome_widget.cpp
        ai_api/system/schema_api.cpp
        ai_api/context/context_engine.cpp
        ai_api/analysis/failure_classifier.cpp
        ai_api/simulation/auto_harness.cpp
        ai_api/simulation/speculative_runner.cpp
        ai_api/context/prompt_builder.cpp
        ai_api/context/rag_context.cpp
        ai_api/analysis/topology_analyzer.cpp
        ai_api/system/feedback_loop.cpp
        ai_api/simulation/simulation_trace.cpp
        ai_api/context/context_budget.cpp
        ai_api/adk/tool_registry.cpp
        ai_api/adk/tool_handlers.cpp
        ai_api/adk/session_manager.cpp
        ai_api/pipeline/rag_pipeline.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_driver_adapter.h
        gui/widgets/engine_console.cpp
        gui/widgets/engine_log_panel.cpp
        gui/analysis/variables_panel.cpp
        gui/analysis/variables_panel.h
        gui/analysis/calculator_widget.cpp
        gui/analysis/calculator_widget.h
        gui/analysis/parametric_sweep_dialog.cpp
        gui/analysis/parametric_sweep_dialog.h

        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        gui/orchestration/rtl_pipeline_panel.cpp
        gui/orchestration/synthesis_result_dialog.cpp
        gui/orchestration/pnr_result_dialog.cpp
        gui/orchestration/signoff_result_dialog.cpp
        gui/editors/physical_overlay_item.cpp
        gui/widgets/cell_pie_chart.h
        gui/widgets/timing_indicator.h

        compute/orchestration/grpc_worker_client.cpp
        gui/analysis/statistical_dashboard.cpp
        gui/analysis/monte_carlo_setup_widget.cpp
        gui/styling/theme_manager.cpp
        gui/core/layout_manager.cpp
        gui/core/storage_manager.cpp
        gui/core/docking_manager.cpp
        gui/widgets/waveform_widget.cpp
        gui/widgets/output_dock_widget.cpp
        gui/widgets/terminal_widget.cpp
        gui/core/platform_helper.cpp
        gui/core/activity_bar.cpp
        gui/core/icon_provider.cpp
        resources/icons.qrc
        gui/analysis/pdk_compliance.cpp
        gui/widgets/compliance_report_widget.cpp
        gui/widgets/command_palette.cpp
        gui/widgets/ghost_diff_viewer.cpp
        gui/widgets/toast_manager.cpp
        gui/core/keybinding_manager.cpp
        gui/widgets/keybindings_dialog.cpp
        gui/widgets/inline_dialog.cpp
        gui/widgets/inline_progress.cpp
        gui/widgets/search_dock.cpp
        gui/widgets/project_model.cpp
        gui/widgets/project_explorer.cpp
        gui/widgets/project_explorer.h
        gui/widgets/library_model.cpp
        gui/widgets/library_model.h
        gui/orchestration/library_index.cpp
        gui/orchestration/library_index.h
        components/design_db.cpp
        gui/analysis/pdk_manager.cpp
        components/netlist_export.cpp
        gui/scripting/tcl_bridge.cpp
        gui/editors/json_viewer.cpp
        gui/editors/image_viewer.cpp
        gui/editors/hex_viewer.cpp
        gui/core/editor_factory.cpp
        gui/orchestration/pdk_manager.cpp
        gui/orchestration/pdk_browser_panel.cpp
    )
    target_include_directories(test_introspection PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/analysis
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/components
        ${CMAKE_CURRENT_SOURCE_DIR}/physics
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
        ${CMAKE_CURRENT_SOURCE_DIR}/components
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/adk
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/pipeline
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
        ${TCL_INCLUDE_PATH}
    )
    target_link_libraries(test_introspection PRIVATE acutesim_orchestration Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Test ${TCL_LIBRARY})
    target_include_directories(test_introspection PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
    )
endif()

# [NEW] Worker Server Executable (Orchestration Backend)
# Requires gRPC and only builds on non-WASM platforms
if(NOT EMSCRIPTEN)
    # 1. Find Protobuf & gRPC Compiler Tools
    find_package(Protobuf REQUIRED)
    find_package(gRPC REQUIRED)
    
    # 2. Define Proto/gRPC Generation Paths
    set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/proto")
    set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/compute/orchestration/proto")
    file(MAKE_DIRECTORY ${PROTO_GEN_DIR})
    
    set(PROTO_FILES "${PROTO_SRC_DIR}/worker_service.proto")
    set(PB_SRCS "${PROTO_GEN_DIR}/worker_service.pb.cc")
    set(PB_HDRS "${PROTO_GEN_DIR}/worker_service.pb.h")
    set(GRPC_SRCS "${PROTO_GEN_DIR}/worker_service.grpc.pb.cc")
    set(GRPC_HDRS "${PROTO_GEN_DIR}/worker_service.grpc.pb.h")

    # 3. Generate Sources (gRPC_CPP_PLUGIN_EXECUTABLE: found with the simulation service's protos above)
    add_custom_command(
        OUTPUT ${PB_SRCS} ${PB_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        ARGS --grpc_out=${PROTO_GEN_DIR}
             --cpp_out=${PROTO_GEN_DIR}
             -I ${PROTO_SRC_DIR}
             --plugin=protoc-gen-grpc=${gRPC_CPP_PLUGIN_EXECUTABLE}
             ${PROTO_FILES}
        DEPENDS ${PROTO_FILES}
    )

    # 4. Define Worker Executable
    add_executable(acutesim-worker
        compute/orchestration/server/worker_server_main.cpp
        compute/orchestration/server/subprocess.cpp
        compute/orchestration/server/subprocess.h
        compute/orchestration/server/worker_service_impl.h
        ${PB_SRCS}
        ${GRPC_SRCS}
        ${PB_HDRS}
        ${GRPC_HDRS}
    )
    
    # Enable C++17 filesystem
    target_compile_features(acutesim-worker PRIVATE cxx_std_17)
    
    target_link_libraries(acutesim-worker PRIVATE 
        gRPC::grpc++ 
        protobuf::libprotobuf
        # No Qt dependency for the worker - keep it lightweight
    )
    
    # Include directories for proto headers
    target_include_directories(acutesim-worker PRIVATE
        ${PROTO_GEN_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # [NEW] Worker Connectivity Test
    add_executable(test_worker_connectivity
        tests/test_worker_connectivity.cpp
        compute/orchestration/grpc_worker_client.cpp
        ${PB_SRCS}
        ${GRPC_SRCS}
        ${PB_HDRS}
        ${GRPC_HDRS}
    )
    target_compile_features(test_worker_connectivity PRIVATE cxx_std_17)
    target_compile_definitions(test_worker_connectivity PRIVATE ACUTESIM_ENABLE_ORCHESTRATION)
    target_link_libraries(test_worker_connectivity PRIVATE 
        gRPC::grpc++ 
        protobuf::libprotobuf
    )
    target_include_directories(test_worker_connectivity PRIVATE
        ${PROTO_GEN_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# Fix Qt platform plugin path for macOS tests (prevents dual-Qt library loading)
# When both 'qt' and 'qtbase' are installed via Homebrew, the platform plugin
# search may find a plugin built against a different Qt version, causing crashes.

# EDA Driver Unit Tests (no external tools required)
add_executable(test_eda_drivers tests/test_eda_drivers.cpp)
target_link_libraries(test_eda_drivers PRIVATE acutesim_orchestration)
target_compile_features(test_eda_drivers PUBLIC cxx_std_17)

# test_rtl_flow — Phase 6 gate check: RTL -> GDS on IHP SG13G2 (skips without the tools)
add_executable(test_rtl_flow tests/test_rtl_flow.cpp)
target_link_libraries(test_rtl_flow PRIVATE acutesim_orchestration)
target_compile_features(test_rtl_flow PUBLIC cxx_std_17)

# test_firmware_on_silicon — Phase 7 gate check: firmware on the core's RTL and routed netlist + SDF
add_executable(test_firmware_on_silicon tests/test_firmware_on_silicon.cpp)
target_link_libraries(test_firmware_on_silicon PRIVATE acutesim_orchestration acutesim_soc)
target_compile_definitions(test_firmware_on_silicon PRIVATE SOC_DATASETS="${CMAKE_CURRENT_SOURCE_DIR}/datasets")
target_compile_features(test_firmware_on_silicon PUBLIC cxx_std_17)

if(APPLE AND CMAKE_PREFIX_PATH)
    set(_qt_plugin_dir "${CMAKE_PREFIX_PATH}/share/qt/plugins")
    if(EXISTS "${_qt_plugin_dir}")
        message(STATUS "Setting QT_PLUGIN_PATH for GUI tests: ${_qt_plugin_dir}")
        set(_qt_gui_tests
            test_hierarchical_sim
            test_gui_logic
            test_docking_interaction
            test_gui_headless_scenarios
        )
        foreach(_test ${_qt_gui_tests})
            if(TEST ${_test})
                set_tests_properties(${_test} PROPERTIES
                    ENVIRONMENT "QT_PLUGIN_PATH=${_qt_plugin_dir}"
                )
            endif()
        endforeach()
    endif()
endif()

# ============================================================================
# test_veriloga_roundtrip — Generator/Parser/Driver round-trip tests
# No Qt::Gui required — pure compute-layer test
# ============================================================================
if(NOT EMSCRIPTEN)
    add_executable(test_veriloga_roundtrip
        tests/test_veriloga_roundtrip.cpp
        components/design_db.cpp
    )
    target_link_libraries(test_veriloga_roundtrip PRIVATE acutesim_orchestration acutesim_compute_core)
    target_include_directories(test_veriloga_roundtrip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(test_veriloga_roundtrip PUBLIC cxx_std_17)
    add_test(NAME test_veriloga_roundtrip COMMAND test_veriloga_roundtrip)
    set_tests_properties(test_veriloga_roundtrip PROPERTIES LABELS "quick")
endif()

# ============================================================================
# test_headless_orchestrator — Proves orchestration works WITHOUT Qt GUI
# Links only acutesim_orchestration (no Qt::Gui, no Qt::Widgets)
# ============================================================================
if(NOT EMSCRIPTEN)
    add_executable(test_headless_orchestrator
        tests/test_headless_orchestrator.cpp
    )
    target_link_libraries(test_headless_orchestrator PRIVATE acutesim_orchestration)
    target_compile_features(test_headless_orchestrator PUBLIC cxx_std_17)
    add_test(NAME test_headless_orchestrator COMMAND test_headless_orchestrator)
    set_tests_properties(test_headless_orchestrator PROPERTIES LABELS "quick")
endif()

if(NOT EMSCRIPTEN)
    add_executable(test_grpc_orchestrator
        tests/test_grpc_orchestrator.cpp
    )
    target_link_libraries(test_grpc_orchestrator PRIVATE acutesim_orchestration acutesim_simulation_proto gRPC::grpc++ protobuf::libprotobuf)
    target_compile_features(test_grpc_orchestrator PUBLIC cxx_std_17)
    add_test(NAME test_grpc_orchestrator COMMAND test_grpc_orchestrator)
    set_tests_properties(test_grpc_orchestrator PROPERTIES LABELS "quick")
endif()

# ============================================================================
# Phase 4: test_analysis_registry — AnalysisRegistry + StagedDispatcher
# ============================================================================
if(NOT EMSCRIPTEN)
    add_executable(test_analysis_registry
        tests/test_analysis_registry.cpp
    )
    target_link_libraries(test_analysis_registry PRIVATE acutesim_engine)
    target_include_directories(test_analysis_registry PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/dto
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/driver
    )
    target_compile_features(test_analysis_registry PUBLIC cxx_std_17)
    add_test(NAME test_analysis_registry COMMAND test_analysis_registry)
    set_tests_properties(test_analysis_registry PROPERTIES LABELS "quick")
endif()

# ============================================================================
# Phase 4: test_parallel_batch — JobScheduler + Parallel BatchExecutionEngine
# ============================================================================
if(NOT EMSCRIPTEN)
    add_executable(test_parallel_batch
        tests/test_parallel_batch.cpp
    )
    target_link_libraries(test_parallel_batch PRIVATE acutesim_sim_layer acutesim_orchestration acutesim_engine)
    target_include_directories(test_parallel_batch PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/dto
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/driver
    )
    target_compile_features(test_parallel_batch PUBLIC cxx_std_17)
    add_test(NAME test_parallel_batch COMMAND test_parallel_batch)
    set_tests_properties(test_parallel_batch PROPERTIES LABELS "quick")
endif()

# ============================================================================
# Phase 4: test_parallel_monte_carlo — MC bit-parity: sequential vs parallel
# ============================================================================
if(NOT EMSCRIPTEN)
    add_executable(test_parallel_monte_carlo
        tests/test_parallel_monte_carlo.cpp
    )
    target_link_libraries(test_parallel_monte_carlo PRIVATE acutesim_sim_layer acutesim_orchestration acutesim_engine)
    target_include_directories(test_parallel_monte_carlo PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/dto
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/driver
    )
    target_compile_features(test_parallel_monte_carlo PUBLIC cxx_std_17)
    add_test(NAME test_parallel_monte_carlo COMMAND test_parallel_monte_carlo)
    set_tests_properties(test_parallel_monte_carlo PROPERTIES LABELS "quick")
endif()

# ============================================================================
# Phase 5: test_gui_headless_scenarios — IGuiDriver + HeadlessDriver + ScenarioRunner
# Drives GridScene(Headless) + LocalWorkerDriver via JSON scenarios. No display needed.
# ============================================================================
if(NOT EMSCRIPTEN)
    add_executable(test_gui_headless_scenarios
        tests/test_gui_headless_scenarios.cpp
        tests/gui_driver/headless_driver.cpp
        tests/gui_driver/scenario_runner.cpp
    )
    target_include_directories(test_gui_headless_scenarios PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_driver
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/dto
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/driver
        ${CMAKE_CURRENT_SOURCE_DIR}/components
    )
    target_link_libraries(test_gui_headless_scenarios PRIVATE
        acutesim_sim_layer
        acutesim_orchestration
        acutesim_engine
        Qt6::Core Qt6::Test
    )
    target_compile_definitions(test_gui_headless_scenarios PRIVATE
        SCENARIOS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/scenarios"
    )
    target_compile_features(test_gui_headless_scenarios PUBLIC cxx_std_17)
    add_test(NAME test_gui_headless_scenarios COMMAND test_gui_headless_scenarios)
    set_tests_properties(test_gui_headless_scenarios PROPERTIES LABELS "heavy")
endif()

# ── CTest Labels: quick (< 5s unit tests) vs heavy (GUI/stress/regression) ──
# Usage: ctest -L quick   (fast feedback loop)
#        ctest -L heavy   (full validation)
set(_quick_tests
    test_physics_kernels test_safe_io test_emulated_f64 test_dual_emulated
    test_fpu_determinism test_determinism
    test_hierarchy_path test_context_budget test_adk_tool_registry
)
foreach(_t ${_quick_tests})
    if(TEST ${_t})
        set_tests_properties(${_t} PROPERTIES LABELS "quick")
    endif()
endforeach()

set(_heavy_tests
    test_hierarchical_sim test_gui_logic test_docking_interaction
    test_ai_simulation_intelligence test_rag_pipeline test_session_manager
)
foreach(_t ${_heavy_tests})
    if(TEST ${_t})
        set_tests_properties(${_t} PROPERTIES LABELS "heavy")
    endif()
endforeach()

# ==============================================================================
# HIERARCHY VERIFICATION TEST (GUI/Headless)
# ==============================================================================
if(NOT EMSCRIPTEN)
    add_executable(test_hierarchy_gui 
        tests/test_hierarchy_gui.cpp 
        ${ACUTESIM_SOURCES}
        compute/orchestration/grpc_worker_client.cpp
    )
    
    target_include_directories(test_hierarchy_gui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors/schematic
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/analysis
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/components
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/styling
        ${CMAKE_CURRENT_SOURCE_DIR}/physics
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
        ${CMAKE_CURRENT_SOURCE_DIR}/components
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/core
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/adk
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/pipeline
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/ai
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/panels
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${TCL_INCLUDE_PATH}
    )
    
    target_link_libraries(test_hierarchy_gui PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Svg
        ${TCL_LIBRARY}
        acutesim_orchestration
    )
    
    if(OpenMP_CXX_FOUND)
        target_link_libraries(test_hierarchy_gui PRIVATE OpenMP::OpenMP_CXX)
    endif()

    if(TARGET gRPC::grpc++)
        target_link_libraries(test_hierarchy_gui PRIVATE 
            gRPC::grpc++ 
            protobuf::libprotobuf
            absl::cord
            absl::strings
            absl::time
            absl::synchronization
            absl::status
        )
    endif()
    
    # Dawn if needed
    if(TARGET dawn_native)
        target_link_libraries(test_hierarchy_gui PRIVATE 
            dawn_native 
            dawn_proc
            webgpu_cpp
        )
        target_compile_definitions(test_hierarchy_gui PRIVATE ACUTESIM_HAS_DAWN_NATIVE)
    endif()
    
    if(Qt6Svg_FOUND)
         target_compile_definitions(test_hierarchy_gui PRIVATE ACUTESIM_HAS_SVG)
    endif()

    # Definitions
    target_compile_definitions(test_hierarchy_gui PRIVATE 
        TEST_MODE 
        ACUTESIM_THREADS_ENABLED
    )
endif()

# Targets compiling gui/core/mainwindow.cpp need what its Reference SoC action uses
if(NOT EMSCRIPTEN)
    foreach(t test_auto_harness test_copilot_validate test_pdk_compliance test_hierarchical_sim
              test_introspection test_hierarchy_gui)
        if(TARGET ${t})
            target_link_libraries(${t} PRIVATE acutesim_soc)
            target_compile_definitions(${t} PRIVATE SOC_DATASETS="${CMAKE_CURRENT_SOURCE_DIR}/datasets")
        endif()
    endforeach()
endif()
