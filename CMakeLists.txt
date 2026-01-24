cmake_minimum_required(VERSION 3.16)
project(AcuteSim VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform-aware default Qt prefix hints (can be overridden by CMAKE_PREFIX_PATH or Qt6_DIR)
if(NOT DEFINED Qt6_DIR)
    if(NOT DEFINED CMAKE_PREFIX_PATH)
        if(APPLE)
            # EXTREMELY IMPORTANT: Prioritize qtbase to avoid mixing with "qt" full package
            # Homebrew's "qt" and "qtbase" often have version mismatches that cause segfaults
            set(CMAKE_PREFIX_PATH "/opt/homebrew/opt/qtbase/lib/cmake")
            message(STATUS "Mac-specific CMAKE_PREFIX_PATH set to: ${CMAKE_PREFIX_PATH}")
        elseif(WIN32)
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
endif()

# Global include directory (Project Root)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Find Qt6 (required)
# Find Qt6 (required)
if(EMSCRIPTEN)
    find_package(Qt6 COMPONENTS Core Gui Widgets Test REQUIRED)
else()
    find_package(Qt6 COMPONENTS Core Gui Widgets Test Concurrent REQUIRED)
endif()

# Find OpenMP (Optional, for physics engine performance)
# Due to security issues with OpenMP on Emscripten, we only find it when not building for Emscripten
if(NOT EMSCRIPTEN)
    find_package(OpenMP)
    
    # Phase 12: Orchestration Dependencies (Provided by vcpkg)
    # Only search for these if not in WASM/Emscripten mode (WASM uses gRPC-Web via JS)
    find_package(protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
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
    gui/core/docking_manager.cpp
    gui/core/docking_manager.cpp
    gui/core/docking_manager.h
    gui/core/platform_helper.cpp
    gui/core/platform_helper.h
    gui/editors/schematic_editor.cpp
    gui/editors/schematic_editor_sync.cpp
    gui/editors/schematic_editor.h
    gui/editors/schematic_view.cpp
    gui/editors/schematic_view.h
    gui/editors/component_wizard.cpp
    gui/editors/component_wizard.h
    gui/components/visual_component.cpp
    gui/components/visual_component.h
    gui/widgets/ai_assistant.cpp
    gui/widgets/ai_assistant.h
    gui/widgets/component_library.cpp
    gui/widgets/component_library.h
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
    gui/analysis/monte_carlo_setup_dialog.cpp
    gui/analysis/monte_carlo_setup_dialog.h
    gui/analysis/statistical_dashboard.cpp
    gui/analysis/statistical_dashboard.h
    physics/simulation_profile.h
    physics/quick_analysis.h
    physics/quick_analysis.cpp
    gui/editors/hdl_editor.cpp
    gui/editors/hdl_editor.h
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
    compute/tensors/physics_tensors.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/solvers/webgpu_solver.cpp
    compute/orchestration/grpc_worker_client.cpp
    compute/orchestration/drivers/yosys_task.cpp
    compute/orchestration/drivers/yosys_task.h
    gui/orchestration/worker_manager.cpp
    gui/orchestration/worker_manager.h
    gui/orchestration/worker_status_panel.cpp
    gui/orchestration/worker_status_panel.h
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
    gui/styling/theme_manager.cpp
    gui/styling/theme_manager.h
    components/circuit.h
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
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Link Qt libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Qt6::Core 
    Qt6::Gui
    Qt6::Widgets
)

if(NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Concurrent)
endif()

# Link OpenMP only if found (and not Wasm)
if(NOT EMSCRIPTEN AND OpenMP_CXX_FOUND)
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
        "-sEXPORTED_RUNTIME_METHODS=['specialHTMLTargets','UTF8ToString','stringToUTF8','ccall','cwrap']"
    )
    # Ensure the output filename is predictable for the HTML shell
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "AcuteSim")
endif()

# Set output name for all platforms (not just Emscripten) if needed, 
# although it already follows PROJECT_NAME.
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "AcuteSim")

# macOS specific: Ensure system frameworks like AGL are found
if(APPLE)
    # Search for the AGL stub specifically to satisfy legacy Qt requirements
    find_file(AGL_TBD NAMES AGL.tbd PATHS 
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/AGL.framework"
        "/Library/Developer/CommandLineTools/SDKs/MacOSX14.4.sdk/System/Library/Frameworks/AGL.framework"
        "/Library/Developer/CommandLineTools/SDKs/MacOSX13.3.sdk/System/Library/Frameworks/AGL.framework"
        "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/AGL.framework"
    )
    
    if(AGL_TBD)
        message(STATUS "Found AGL stub: ${AGL_TBD}")
        # Get the directory containing AGL.framework
        get_filename_component(AGL_FRAMEWORK_DIR "${AGL_TBD}" DIRECTORY)
        get_filename_component(AGL_SDK_FRAMEWORKS_PATH "${AGL_FRAMEWORK_DIR}" DIRECTORY)
        
        # Link the stub and add the search path for the framework
        target_link_libraries(${PROJECT_NAME} PRIVATE ${AGL_TBD})
        target_link_options(${PROJECT_NAME} PRIVATE "-F${AGL_SDK_FRAMEWORKS_PATH}")
    else()
        # Fallback to searching as framework if stub is not found
        find_library(AGL_LIBRARY AGL)
        if(AGL_LIBRARY)
            target_link_libraries(${PROJECT_NAME} PRIVATE ${AGL_LIBRARY})
        endif()
    endif()
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


# Sensitivity Analysis Test (AD Gradient Check)
add_executable(test_sensitivity 
    tests/test_sensitivity.cpp
    compute/tensors/physics_tensors.cpp
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

# Auto-Validation Harness Test (ERC + AI API)
if(NOT EMSCRIPTEN)
    add_executable(test_auto_harness
        tests/test_auto_harness.cpp
        gui/components/visual_component.cpp
        gui/editors/schematic_editor.cpp
        gui/editors/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic_editor_api.cpp
        gui/editors/schematic_editor_sync.cpp
        gui/widgets/ai_assistant.cpp
        gui/widgets/component_library.cpp
        gui/widgets/design_flow.cpp
        gui/analysis/system_inspector.cpp
        gui/analysis/logic_analyzer.cpp
        gui/editors/hdl_editor.cpp
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
        compute/tensors/physics_tensors.cpp
        compute/solvers/superposition.cpp
        compute/solvers/circuitsim_solver.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        physics/quick_analysis.cpp
        compute/orchestration/grpc_worker_client.cpp
        gui/analysis/statistical_dashboard.cpp
        gui/analysis/monte_carlo_setup_dialog.cpp
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
    )
    target_link_libraries(test_auto_harness PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent)
    target_compile_features(test_auto_harness PUBLIC cxx_std_17)
endif()

# Copilot Integration Test
if(NOT EMSCRIPTEN)
    add_executable(test_copilot_validate
        tests/test_copilot_validate.cpp
        gui/components/visual_component.cpp
        gui/editors/schematic_editor.cpp
        gui/editors/schematic_view.cpp
        gui/editors/component_wizard.cpp
        gui/editors/schematic_editor_api.cpp
        gui/editors/schematic_editor_sync.cpp
        gui/widgets/ai_assistant.cpp
        gui/widgets/component_library.cpp
        gui/widgets/design_flow.cpp
        gui/analysis/system_inspector.cpp
        gui/analysis/logic_analyzer.cpp
        gui/editors/hdl_editor.cpp
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
        compute/tensors/physics_tensors.cpp
        compute/solvers/superposition.cpp
        compute/solvers/circuitsim_solver.cpp
        gui/analysis/simulation_workspace.cpp
        gui/analysis/simulation_explorer.cpp
        gui/analysis/corner_setup_widget.cpp
        gui/widgets/problems_panel.cpp
        gui/widgets/debug_suggestions_panel.cpp
        gui/widgets/trust_badge_widget.cpp
        gui/orchestration/worker_status_panel.cpp
        gui/orchestration/worker_manager.cpp
        physics/quick_analysis.cpp
        compute/orchestration/grpc_worker_client.cpp
        gui/analysis/statistical_dashboard.cpp
        gui/analysis/monte_carlo_setup_dialog.cpp
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
    )
    target_link_libraries(test_copilot_validate PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent)
    target_compile_features(test_copilot_validate PUBLIC cxx_std_17)
endif()

# ERC Rules Automated Test
add_executable(test_erc_rules 
    tests/test_erc_rules.cpp
    compute/tensors/physics_tensors.cpp
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
        compute/tensors/physics_tensors.cpp
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

# Phase 1.7 Simulation Suite Tests
add_executable(test_simulation 
    tests/simulation_test_main.cpp
    physics/quick_analysis.cpp
    compute/tensors/physics_tensors.cpp
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
endif()

# =============================================================================
# Testing Configuration
# =============================================================================
enable_testing()

add_test(NAME test_safe_io COMMAND test_safe_io)
add_test(NAME test_ai_simulation_intelligence COMMAND test_ai_simulation_intelligence)

# Phase 5 GUI Logic Test
add_executable(test_gui_logic tests/test_gui_logic.cpp gui/components/visual_component.cpp gui/styling/theme_manager.cpp gui/core/platform_helper.cpp)
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
add_executable(test_docking_interaction tests/test_docking_interaction.cpp gui/core/layout_manager.cpp gui/widgets/terminal_widget.cpp gui/widgets/output_dock_widget.cpp gui/widgets/waveform_widget.cpp gui/styling/theme_manager.cpp gui/core/docking_manager.cpp)
target_include_directories(test_docking_interaction PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/core
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/styling
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_docking_interaction PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME test_docking_interaction COMMAND test_docking_interaction)

