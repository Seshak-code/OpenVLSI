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
    add_compile_options(-fno-fast-math -frounding-math)
    
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
    set(DAWN_ENABLE_METAL ON CACHE BOOL "" FORCE)
    set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
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
    
    if(TARGET dawn_internal_config)
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
add_executable(${PROJECT_NAME} 
    main.cpp
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
    gui/core/hierarchy_path.cpp
    gui/core/hierarchy_path.h
    resources/icons.qrc
    gui/editors/schematic_editor.cpp
    gui/editors/schematic_editor_sync.cpp
    components/netlist_export.cpp
    gui/editors/schematic_editor.h
    gui/editors/code_editor.cpp
    gui/editors/code_editor.h
    gui/core/sim_auto_detector.cpp
    gui/core/sim_auto_detector.h
    gui/editors/schematic_view.cpp
    gui/editors/schematic_view.h
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
    components/pdk_compliance.cpp
    components/pdk_compliance.h
    components/pdk_manager.cpp
    components/pdk_manager.h
    gui/editors/schematic_editor_api.cpp
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
    gui/analysis/simulation_worker.cpp
    gui/analysis/simulation_worker.h
    gui/analysis/monte_carlo_setup_widget.cpp
    gui/analysis/monte_carlo_setup_widget.h
    gui/analysis/statistical_dashboard.cpp
    gui/analysis/statistical_dashboard.h
    physics/simulation_profile.h
    physics/quick_analysis.h
    physics/quick_analysis.cpp
    physics/quick_analysis.cpp
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
    ai_api/analysis/failure_classifier.cpp
    ai_api/analysis/failure_classifier.h
    ai_api/simulation/speculative_runner.cpp
    ai_api/simulation/speculative_runner.h
    ai_api/context/prompt_builder.cpp
    ai_api/context/prompt_builder.h
    ai_api/context/rag_context.cpp
    ai_api/context/rag_context.h
    ai_api/analysis/topology_analyzer.cpp
    ai_api/analysis/topology_analyzer.h
    ai_api/system/feedback_loop.cpp
    ai_api/system/feedback_loop.h
    ai_api/simulation/auto_harness.cpp
    ai_api/simulation/auto_harness.h
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
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/solvers/webgpu_solver.cpp
    compute/orchestration/grpc_worker_client.cpp
    compute/orchestration/drivers/yosys_task.cpp
    compute/orchestration/drivers/yosys_task.h
    compute/orchestration/drivers/openroad_task.cpp
    compute/orchestration/drivers/openroad_task.h
    compute/orchestration/drivers/klayout_task.cpp
    compute/orchestration/drivers/klayout_task.h
    gui/orchestration/worker_manager.cpp
    gui/orchestration/worker_manager.h
    gui/orchestration/worker_status_panel.cpp
    gui/orchestration/worker_status_panel.h
    gui/orchestration/rtl_pipeline_panel.cpp
    gui/orchestration/rtl_pipeline_panel.h
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
    gui/widgets/project_explorer.cpp
    gui/widgets/project_explorer.h
    gui/widgets/search_dock.cpp
    gui/widgets/search_dock.h
    components/circuit.h
    components/design_db.cpp
    gui/scripting/tcl_bridge.cpp
    gui/scripting/tcl_bridge.h
    gui/core/subcircuit_resolver.cpp
    gui/components/standard_cell_visualizer.cpp
)

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
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
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
    find_package(TCL REQUIRED)
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
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Concurrent)
else()
    # Export runtime methods but do NOT generate HTML (we manage it manually)
    set_target_properties(${PROJECT_NAME} PROPERTIES 
        LINK_OPTIONS "SHELL:-s EXPORTED_RUNTIME_METHODS=['printErr','ccall','cwrap'] -s ALLOW_MEMORY_GROWTH=1 -s ASSERTIONS=1"
    )
endif()

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
        "-sEXIT_RUNTIME=1"
        "-sERROR_ON_UNDEFINED_SYMBOLS=0"
        "-sUSE_WEBGPU=1"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=AcuteSim"
        "-sEXPORTED_RUNTIME_METHODS=['specialHTMLTargets','UTF8ToString','UTF16ToString','stringToUTF8','stringToUTF16','ccall','cwrap','wasmMemory','FS','callMain','ENV']"
        "-sEXPORTED_FUNCTIONS=['_main','_dumpStorageFile','_malloc','_free']"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/resources@/resources"
    )
    


    if(ACUTESIM_WASM_THREADS)
        target_link_options(${PROJECT_NAME} PRIVATE "-pthread" "-sUSE_PTHREADS=1" "-sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency")
        add_compile_options("-pthread")
        target_compile_definitions(${PROJECT_NAME} PRIVATE ACUTESIM_THREADS_ENABLED)
    endif()

    target_link_options(${PROJECT_NAME} PRIVATE "-sPROXY_TO_WORKER=0")
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
add_executable(test_regression 
    tests/regression_main.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(test_regression PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_regression PRIVATE Qt6::Core)
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


# Noise Analysis Test
add_executable(test_noise_analysis
    tests/test_noise_analysis.cpp
)
target_include_directories(test_noise_analysis PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/autodiff
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(test_noise_analysis PUBLIC cxx_std_17)

# Convergence Engine Test (NR failure classification)
add_executable(test_convergence_engine
    tests/test_convergence_engine.cpp
)
target_include_directories(test_convergence_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(test_convergence_engine PUBLIC cxx_std_17)

# AD Integration Test (Dual<T> vs Finite-Difference for all devices)
add_executable(test_ad_integration
    tests/test_ad_integration.cpp
)
target_include_directories(test_ad_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/autodiff
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(test_ad_integration PUBLIC cxx_std_17)

# Sensitivity Analysis Test (AD Gradient Check)
add_executable(test_sensitivity 
    tests/test_sensitivity.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(test_sensitivity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/autodiff
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_sensitivity PRIVATE Qt6::Core)
target_compile_features(test_sensitivity PUBLIC cxx_std_17)

# Superposition Test
add_executable(test_superposition 
    tests/test_superposition.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(test_superposition PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_superposition PRIVATE Qt6::Core)
target_compile_features(test_superposition PUBLIC cxx_std_17)

# Mode Ladder Regression Test
add_executable(test_mode_ladder 
    tests/test_mode_ladder.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(test_mode_ladder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_mode_ladder PRIVATE Qt6::Core)
target_compile_features(test_mode_ladder PUBLIC cxx_std_17)

# Auto-Validation Harness Test (ERC + AI API)
if(NOT EMSCRIPTEN)
    add_executable(test_auto_harness
        tests/test_auto_harness.cpp
        gui/components/visual_component.cpp
        gui/components/probe_tip.cpp
        gui/core/hierarchy_path.cpp
        gui/widgets/breadcrumb_widget.cpp
        gui/editors/schematic_editor.cpp
        gui/editors/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic_editor_api.cpp
        gui/editors/schematic_editor_sync.cpp
        gui/panels/ai_assistant_panel.cpp
        gui/widgets/component_library.cpp
        gui/widgets/design_flow.cpp
        gui/analysis/system_inspector.cpp
        gui/analysis/logic_analyzer.cpp
        gui/editors/code_editor.cpp
        gui/core/editor_manager.cpp
        gui/core/sim_auto_detector.cpp
        gui/core/mainwindow.cpp
        gui/core/welcome_widget.cpp
        gui/components/standard_cell_visualizer.cpp
        gui/core/subcircuit_resolver.cpp
        components/design_db.cpp
        components/pdk_manager.cpp
    gui/scripting/tcl_bridge.cpp
    components/pdk_compliance.cpp
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
        compute/tensors/physics_tensors.cpp
        compute/tensors/kernel_dispatch.cpp
        compute/tensors/physics_kernels_avx512.cpp
        compute/solvers/superposition.cpp
        compute/solvers/circuitsim_solver.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_worker.cpp
        gui/analysis/simulation_worker.h
        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        gui/orchestration/rtl_pipeline_panel.cpp
        compute/orchestration/drivers/yosys_task.cpp
        compute/orchestration/drivers/openroad_task.cpp
        compute/orchestration/drivers/klayout_task.cpp
        physics/quick_analysis.cpp
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
    target_link_libraries(test_auto_harness PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent ${TCL_LIBRARY})
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
        gui/editors/schematic_editor.cpp
        gui/editors/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic_editor_api.cpp
        gui/editors/schematic_editor_sync.cpp
        gui/panels/ai_assistant_panel.cpp
        gui/widgets/component_library.cpp
        gui/widgets/design_flow.cpp
        gui/analysis/system_inspector.cpp
        gui/analysis/logic_analyzer.cpp
        gui/editors/code_editor.cpp
        gui/core/editor_manager.cpp
        gui/core/sim_auto_detector.cpp
        gui/core/mainwindow.cpp
        gui/core/subcircuit_resolver.cpp
        gui/components/standard_cell_visualizer.cpp
    gui/scripting/tcl_bridge.cpp
    components/pdk_compliance.cpp
    gui/widgets/compliance_report_widget.cpp
        gui/core/welcome_widget.cpp
        components/design_db.cpp
        components/pdk_manager.cpp
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
        compute/tensors/physics_tensors.cpp
        compute/tensors/kernel_dispatch.cpp
        compute/tensors/physics_kernels_avx512.cpp
        compute/solvers/superposition.cpp
        compute/solvers/circuitsim_solver.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_worker.cpp
        gui/analysis/simulation_worker.h
        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        gui/orchestration/rtl_pipeline_panel.cpp
        compute/orchestration/drivers/yosys_task.cpp
        compute/orchestration/drivers/openroad_task.cpp
        compute/orchestration/drivers/klayout_task.cpp
        physics/quick_analysis.cpp
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
    target_link_libraries(test_copilot_validate PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent ${TCL_LIBRARY})
    target_compile_features(test_copilot_validate PUBLIC cxx_std_17)
endif()

# ERC Rules Automated Test
add_executable(test_erc_rules 
    tests/test_erc_rules.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(test_erc_rules PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_erc_rules PRIVATE Qt6::Core)
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
        compute/solvers/circuitsim_solver.cpp
        compute/solvers/superposition.cpp
        compute/tensors/physics_tensors.cpp
        compute/tensors/kernel_dispatch.cpp
        compute/tensors/physics_kernels_avx512.cpp
    )
    target_include_directories(test_ai_simulation_intelligence PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
        ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
        ${CMAKE_CURRENT_SOURCE_DIR}/physics
        ${CMAKE_CURRENT_SOURCE_DIR}/components
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    )
    target_link_libraries(test_ai_simulation_intelligence PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent)
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
add_executable(test_simulation 
    tests/simulation_test_main.cpp
    physics/quick_analysis.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(test_simulation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_compile_features(test_simulation PUBLIC cxx_std_17)
target_link_libraries(test_simulation PRIVATE Qt6::Core)

# BJT Thermal Verification
add_executable(verify_bjt_thermal 
    tests/verify_bjt_thermal.cpp
    physics/quick_analysis.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(verify_bjt_thermal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_compile_features(verify_bjt_thermal PUBLIC cxx_std_17)
target_link_libraries(verify_bjt_thermal PRIVATE Qt6::Core)

# Dependent Source Test
add_executable(test_dependent_simulation 
    tests/test_dependent_simulation.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(test_dependent_simulation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_dependent_simulation PRIVATE Qt6::Core)
target_compile_features(test_dependent_simulation PUBLIC cxx_std_17)

# SOA Bridge Consistency Test
add_executable(test_soa_bridge 
    tests/test_soa_bridge.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(test_soa_bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_soa_bridge PRIVATE Qt6::Core)
target_compile_features(test_soa_bridge PUBLIC cxx_std_17)
target_include_directories(test_dependent_simulation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_dependent_simulation PRIVATE Qt6::Core)
target_compile_features(test_dependent_simulation PUBLIC cxx_std_17)

# Test PDK Manager
add_executable(test_pdk_manager tests/test_pdk_manager.cpp components/pdk_manager.cpp)
target_include_directories(test_pdk_manager PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/components
)
target_link_libraries(test_pdk_manager PRIVATE Qt6::Core Qt6::Test)
target_link_libraries(test_pdk_manager PRIVATE Qt6::Core Qt6::Test)
target_compile_features(test_pdk_manager PUBLIC cxx_std_17)

# Test PDK Swap (Regression)
add_executable(test_pdk_swap
    tests/test_pdk_swap.cpp
    components/pdk_manager.cpp
    components/pdk_compliance.cpp
    components/netlist_export.cpp

    gui/components/visual_component.cpp

    gui/components/probe_tip.cpp
    gui/styling/theme_manager.cpp
    gui/styling/theme_manager.h
    gui/core/hierarchy_path.cpp
    gui/widgets/breadcrumb_widget.cpp
    tests/stubs/grid_scene_stub.cpp # Stub implementation
    components/design_db.cpp
    gui/editors/schematic_editor.h # Triggers MOC for GridScene
    gui/core/editor_manager.cpp
    gui/core/sim_auto_detector.cpp
    gui/editors/code_editor.cpp
    gui/core/icon_provider.cpp
)
target_include_directories(test_pdk_swap PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
)
target_link_libraries(test_pdk_swap PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
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
    target_link_options(test_superposition PRIVATE ${EMSCRIPTEN_TEST_LINK_OPTIONS})
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
        gui/editors/schematic_editor.cpp
        gui/editors/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic_editor_api.cpp
        gui/editors/schematic_editor_sync.cpp
        gui/panels/ai_assistant_panel.cpp
        gui/widgets/component_library.cpp
        gui/widgets/design_flow.cpp
        gui/analysis/system_inspector.cpp
        gui/editors/code_editor.cpp
        gui/core/editor_manager.cpp
        gui/core/sim_auto_detector.cpp
        gui/core/mainwindow.cpp
        gui/core/welcome_widget.cpp
        gui/components/standard_cell_visualizer.cpp
        gui/core/subcircuit_resolver.cpp
        components/design_db.cpp
        components/pdk_manager.cpp
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
        compute/tensors/physics_tensors.cpp
        compute/tensors/kernel_dispatch.cpp
        compute/tensors/physics_kernels_avx512.cpp
        compute/solvers/superposition.cpp
        compute/solvers/circuitsim_solver.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_worker.cpp
        gui/analysis/simulation_worker.h
        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        gui/orchestration/rtl_pipeline_panel.cpp
        compute/orchestration/drivers/yosys_task.cpp
        compute/orchestration/drivers/openroad_task.cpp
        compute/orchestration/drivers/klayout_task.cpp
        physics/quick_analysis.cpp
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
        gui/widgets/project_explorer.cpp
        components/pdk_compliance.cpp
        components/pdk_manager.cpp
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
    target_link_libraries(test_pdk_compliance PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent ${TCL_LIBRARY})
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
        gui/editors/schematic_editor.cpp
        gui/editors/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic_editor_api.cpp
        gui/editors/schematic_editor_sync.cpp
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
        compute/tensors/physics_tensors.cpp
        compute/tensors/kernel_dispatch.cpp
        compute/tensors/physics_kernels_avx512.cpp
        compute/solvers/superposition.cpp
        compute/solvers/circuitsim_solver.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_worker.cpp
        gui/analysis/simulation_worker.h
        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        gui/orchestration/rtl_pipeline_panel.cpp
        compute/orchestration/drivers/yosys_task.cpp
        compute/orchestration/drivers/openroad_task.cpp
        compute/orchestration/drivers/klayout_task.cpp
        physics/quick_analysis.cpp
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
        components/pdk_compliance.cpp
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
        components/design_db.cpp
        components/pdk_manager.cpp
        components/netlist_export.cpp
        gui/scripting/tcl_bridge.cpp
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
    target_link_libraries(test_hierarchical_sim PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent ${TCL_LIBRARY})
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
    target_link_libraries(test_spatial_logic PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui ${TCL_LIBRARY})
    # Link against app objects or sources?
    # Currently CuteApp sources are scattered. Best to add them or link a library if available.
    # Since we don't have a library target for gui/components, we must add sources interactively.
    target_sources(test_spatial_logic PRIVATE
        gui/editors/schematic_editor.cpp
        gui/editors/schematic_editor_api.cpp
        gui/editors/schematic_editor_sync.cpp
        gui/editors/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/components/visual_component.cpp
        gui/components/probe_tip.cpp
        gui/styling/theme_manager.cpp
        gui/core/editor_manager.cpp
        gui/core/sim_auto_detector.cpp
        gui/core/hierarchy_path.cpp
        gui/widgets/breadcrumb_widget.cpp
        gui/editors/code_editor.cpp
        gui/core/icon_provider.cpp
        gui/widgets/compliance_report_widget.cpp
        gui/components/standard_cell_visualizer.cpp
        gui/core/subcircuit_resolver.cpp
        components/design_db.cpp
        components/pdk_manager.cpp
        components/netlist_export.cpp
        components/pdk_compliance.cpp
        gui/scripting/tcl_bridge.cpp
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
target_link_libraries(test_gui_logic PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
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
add_executable(test_determinism 
    tests/test_determinism.cpp
    physics/quick_analysis.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(test_determinism PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_compile_features(test_determinism PUBLIC cxx_std_17)
target_link_libraries(test_determinism PRIVATE Qt6::Core)
add_test(NAME test_determinism COMMAND test_determinism)

# Test Kernel Dispatch
add_executable(test_kernel_dispatch 
    tests/test_kernel_dispatch.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/physics_kernels_avx512.cpp
)
target_include_directories(test_kernel_dispatch PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/components
)
target_link_libraries(test_kernel_dispatch PRIVATE Qt6::Core)
target_compile_features(test_kernel_dispatch PUBLIC cxx_std_17)
add_test(NAME test_kernel_dispatch COMMAND test_kernel_dispatch)



# Verification Harness (Benchmarks, Alignment, Dense Batch)
add_executable(test_verification_harness 
    tests/verification_main.cpp
    physics/quick_analysis.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(test_verification_harness PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_compile_features(test_verification_harness PUBLIC cxx_std_17)
target_link_libraries(test_verification_harness PRIVATE Qt6::Core)
# Standard Cell Verification (Generic Darlington)
add_executable(test_std_cells 
    tests/test_std_cells.cpp
    physics/quick_analysis.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
)
target_include_directories(test_std_cells PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_compile_features(test_std_cells PUBLIC cxx_std_17)
target_link_libraries(test_std_cells PRIVATE Qt6::Core)

if(NOT EMSCRIPTEN)
    # Validating flow tests
    add_executable(test_flow tests/test_flow.cpp)
    target_link_libraries(test_flow PRIVATE Qt6::Core)
    target_include_directories(test_flow PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/components 
        ${CMAKE_CURRENT_SOURCE_DIR}/physics
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
        gui/editors/schematic_editor.cpp
        gui/editors/schematic_editor_api.cpp
        gui/core/editor_manager.cpp
        gui/components/visual_component.cpp
        gui/components/probe_tip.cpp
        gui/editors/schematic_editor.cpp
        gui/editors/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic_editor_api.cpp
        gui/editors/schematic_editor_sync.cpp
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
        compute/tensors/physics_tensors.cpp
        compute/tensors/kernel_dispatch.cpp
        compute/tensors/physics_kernels_avx512.cpp
        compute/solvers/superposition.cpp
        compute/solvers/circuitsim_solver.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_worker.cpp
        gui/analysis/simulation_worker.h
        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        gui/orchestration/rtl_pipeline_panel.cpp
        compute/orchestration/drivers/yosys_task.cpp
        compute/orchestration/drivers/openroad_task.cpp
        compute/orchestration/drivers/klayout_task.cpp
        physics/quick_analysis.cpp
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
        components/pdk_compliance.cpp
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
        components/design_db.cpp
        components/pdk_manager.cpp
        components/netlist_export.cpp
        gui/scripting/tcl_bridge.cpp
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
    target_link_libraries(test_introspection PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Test ${TCL_LIBRARY})
    target_include_directories(test_flow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compute/math)
    target_include_directories(test_introspection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compute/math)
    target_include_directories(test_flow PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    )
    target_include_directories(test_introspection PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    )
    target_sources(test_flow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors/kernel_dispatch.cpp)
    target_sources(test_introspection PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors/kernel_dispatch.cpp)
    target_include_directories(test_introspection PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/editors
        ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
    )
endif()

# GPU Connectivity Verification (Native Dawn)
if(NOT EMSCRIPTEN)
    add_executable(test_gpu_connectivity 
        tests/test_gpu_connectivity.cpp
        compute/solvers/webgpu_solver.cpp
        compute/tensors/physics_tensors.cpp
        compute/tensors/kernel_dispatch.cpp
    )
    target_include_directories(test_gpu_connectivity PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
        ${CMAKE_CURRENT_SOURCE_DIR}/components
        ${DAWN_INTERNAL_INCLUDES}
    )
    target_link_libraries(test_gpu_connectivity PRIVATE 
        Qt6::Core 
        dawn_native 
        dawn_proc
        webgpu_cpp
    )
    target_compile_definitions(test_gpu_connectivity PRIVATE ACUTESIM_HAS_DAWN_NATIVE)
    target_compile_features(test_gpu_connectivity PUBLIC cxx_std_17)
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

    # 3. Generate Sources
    set(gRPC_CPP_PLUGIN_EXECUTABLE "/opt/homebrew/bin/grpc_cpp_plugin")
    
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
if(APPLE AND CMAKE_PREFIX_PATH)
    set(_qt_plugin_dir "${CMAKE_PREFIX_PATH}/share/qt/plugins")
    if(EXISTS "${_qt_plugin_dir}")
        message(STATUS "Setting QT_PLUGIN_PATH for GUI tests: ${_qt_plugin_dir}")
        set(_qt_gui_tests
            test_hierarchical_sim
            test_gui_logic
            test_docking_interaction
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
