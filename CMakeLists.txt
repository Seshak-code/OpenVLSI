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

# Find Qt6 (required)
# Find Qt6 (required)
find_package(Qt6 COMPONENTS Core Gui Widgets Test REQUIRED)

# Find OpenMP (Optional, for physics engine performance)
# Due to security issues with OpenMP on Emscripten, we only find it when not building for Emscripten
if(NOT EMSCRIPTEN)
    find_package(OpenMP)
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
    gui/analysis/simulator_window.cpp
    gui/analysis/simulator_window.h
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
    gui/analysis/logic_analyzer.cpp
    gui/analysis/logic_analyzer.h
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
    components/circuit.h
)

target_include_directories(${PROJECT_NAME} PUBLIC
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
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/core
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/context
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/analysis
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/simulation
    ${CMAKE_CURRENT_SOURCE_DIR}/ai_api/system
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# Link Qt libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Qt6::Core 
    Qt6::Gui
    Qt6::Widgets
)

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
)
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
)
target_compile_features(test_superposition PUBLIC cxx_std_17)

# Auto-Validation Harness Test (ERC + AI API)
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
    gui/analysis/simulator_window.cpp
    gui/editors/hdl_editor.cpp
    gui/core/mainwindow.cpp
    gui/core/welcome_widget.cpp
    ai_api/system/schema_api.cpp
    ai_api/context/context_engine.cpp
    ai_api/analysis/failure_classifier.cpp
    ai_api/simulation/auto_harness.cpp
    ai_api/simulation/speculative_runner.cpp
    ai_api/context/prompt_builder.cpp
    ai_api/analysis/topology_analyzer.cpp
    ai_api/system/feedback_loop.cpp
    ai_api/simulation/simulation_trace.cpp
    compute/tensors/physics_tensors.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
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
target_link_libraries(test_auto_harness PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_compile_features(test_auto_harness PUBLIC cxx_std_17)

# Copilot Integration Test
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
    gui/analysis/simulator_window.cpp
    gui/editors/hdl_editor.cpp
    gui/core/mainwindow.cpp
    gui/core/welcome_widget.cpp
    ai_api/system/schema_api.cpp
    ai_api/context/context_engine.cpp
    ai_api/analysis/failure_classifier.cpp
    ai_api/simulation/auto_harness.cpp
    ai_api/simulation/speculative_runner.cpp
    ai_api/context/prompt_builder.cpp
    ai_api/analysis/topology_analyzer.cpp
    ai_api/system/feedback_loop.cpp
    ai_api/simulation/simulation_trace.cpp
    compute/tensors/physics_tensors.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
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
target_link_libraries(test_copilot_validate PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_compile_features(test_copilot_validate PUBLIC cxx_std_17)

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
target_compile_features(test_erc_rules PUBLIC cxx_std_17)
