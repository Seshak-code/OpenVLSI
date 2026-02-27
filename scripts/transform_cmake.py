#!/usr/bin/env python3
"""
transform_cmake.py — Refactors CMakeLists.txt to introduce acutesim_compute_core
and acutesim_orchestration STATIC libraries, and simplifies test targets to link
against these libraries instead of listing sources redundantly.
"""

import sys
import re

CMAKE_PATH = "/Users/seshak/Desktop/CuteApp/CMakeLists.txt"

def read_file(path):
    with open(path, "r") as f:
        return f.read()

def write_file(path, content):
    with open(path, "w") as f:
        f.write(content)

def report(label, ok, old=None, new=None):
    status = "OK" if ok else "FAIL"
    print(f"[{status}] {label}")
    if not ok and old is not None:
        print(f"  Pattern not found (first 120 chars): {repr(old[:120])}")

def replace_exact(content, old, new, label, count=1):
    """Replace exactly `count` occurrences. Report success/failure."""
    n = content.count(old)
    if n == 0:
        report(label, False, old)
        return content, False
    if count is not None and n != count:
        print(f"[WARN] {label}: expected {count} occurrence(s) but found {n}; replacing all")
    result = content.replace(old, new, count if count is not None else n)
    report(label, True)
    return result, True


# ─────────────────────────────────────────────────────────────────────────────
# STEP A: Insert library definitions BEFORE qt_add_executable
# ─────────────────────────────────────────────────────────────────────────────

LIBRARY_BLOCK = '''\
# ============================================================================
# acutesim_compute_core — Physics + Compute kernel STATIC library
# Exposes: physics/, autodiff/, components/, compute/* headers as PUBLIC
# ============================================================================
add_library(acutesim_compute_core STATIC
    physics/quick_analysis.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
    compute/infrastructure/netlist_compiler_ctx.cpp
    compute/infrastructure/postproc_engine.cpp
)
target_include_directories(acutesim_compute_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/autodiff
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(acutesim_compute_core PUBLIC Qt6::Core)
target_compile_features(acutesim_compute_core PUBLIC cxx_std_17)

# ============================================================================
# acutesim_orchestration — Orchestration driver + EDA task STATIC library
# PUBLIC include: compute/orchestration/ (exposes dto/ and driver/ headers)
# PRIVATE link: acutesim_compute_core (physics headers don't leak upward)
# ============================================================================
if(NOT EMSCRIPTEN)
    add_library(acutesim_orchestration STATIC
        compute/orchestration/driver/local_worker_driver.cpp
        compute/orchestration/batch_runner.cpp
        compute/orchestration/simulation_context.cpp
        compute/orchestration/drivers/yosys_task.cpp
        compute/orchestration/drivers/openroad_task.cpp
        compute/orchestration/drivers/klayout_task.cpp
        compute/orchestration/drivers/yosys_log_parser.cpp
        compute/orchestration/drivers/openroad_report_parser.cpp
        compute/orchestration/drivers/klayout_report_parser.cpp
        compute/orchestration/drivers/tool_driver.cpp
        compute/orchestration/server/subprocess.cpp
    )
    target_include_directories(acutesim_orchestration PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
    )
    target_link_libraries(acutesim_orchestration
        PRIVATE acutesim_compute_core
        PUBLIC Qt6::Core
    )
    target_compile_features(acutesim_orchestration PUBLIC cxx_std_17)
endif()

qt_add_executable(${PROJECT_NAME} main.cpp ${ACUTESIM_SOURCES})'''

STEP_A_OLD = "qt_add_executable(${PROJECT_NAME} main.cpp ${ACUTESIM_SOURCES})"

def step_a(content):
    old = STEP_A_OLD
    new = LIBRARY_BLOCK
    return replace_exact(content, old, new, "Step A: Insert library definitions before qt_add_executable")


# ─────────────────────────────────────────────────────────────────────────────
# STEP B: Remove sources from ACUTESIM_SOURCES block only
# We locate the set(ACUTESIM_SOURCES ... ) block and strip lines inside it.
# ─────────────────────────────────────────────────────────────────────────────

ACUTESIM_SOURCES_TO_REMOVE = [
    "    physics/quick_analysis.cpp\n",
    "    compute/tensors/physics_tensors.cpp\n",
    "    compute/tensors/kernel_dispatch.cpp\n",
    "    compute/tensors/physics_kernels_avx512.cpp\n",
    "    compute/tensors/physics_kernels_avx2.cpp\n",
    "    compute/solvers/superposition.cpp\n",
    "    compute/solvers/circuitsim_solver.cpp\n",
    "    compute/infrastructure/netlist_compiler.cpp\n",
    "    compute/infrastructure/netlist_compiler_ctx.cpp\n",
    "    compute/infrastructure/postproc_engine.cpp\n",
    "    compute/infrastructure/postproc_engine.h\n",
    "    compute/orchestration/driver/local_worker_driver.cpp\n",
    "    compute/orchestration/batch_runner.cpp\n",
    "    compute/orchestration/batch_runner.h\n",
    "    compute/orchestration/simulation_context.cpp\n",
    "    compute/orchestration/simulation_context.h\n",
    "    compute/orchestration/drivers/yosys_task.cpp\n",
    "    compute/orchestration/drivers/yosys_task.h\n",
    "    compute/orchestration/drivers/openroad_task.cpp\n",
    "    compute/orchestration/drivers/openroad_task.h\n",
    "    compute/orchestration/drivers/klayout_task.cpp\n",
    "    compute/orchestration/drivers/klayout_task.h\n",
    "    compute/orchestration/drivers/yosys_log_parser.cpp\n",
    "    compute/orchestration/drivers/yosys_log_parser.h\n",
    "    compute/orchestration/drivers/openroad_report_parser.cpp\n",
    "    compute/orchestration/drivers/openroad_report_parser.h\n",
    "    compute/orchestration/drivers/klayout_report_parser.cpp\n",
    "    compute/orchestration/drivers/klayout_report_parser.h\n",
    "    compute/orchestration/drivers/tool_driver.cpp\n",
    "    compute/orchestration/drivers/tool_driver.h\n",
    "    compute/orchestration/server/subprocess.cpp\n",
]

def step_b(content):
    # Find the ACUTESIM_SOURCES block boundaries
    start_marker = "set(ACUTESIM_SOURCES\n"
    start_idx = content.find(start_marker)
    if start_idx == -1:
        report("Step B: Locate ACUTESIM_SOURCES block", False)
        return content

    # Find the closing ')' that ends the set() call.
    # It's the first ')' on its own line after the block starts.
    search_from = start_idx + len(start_marker)
    # The block ends with a line that is just ")\n"
    end_idx = content.find("\n)\n", search_from)
    if end_idx == -1:
        report("Step B: Find closing ) of ACUTESIM_SOURCES", False)
        return content
    end_idx += 1  # include the newline before )
    block_end = end_idx + 2  # position after ")\n"

    block = content[start_idx:block_end]
    original_block = block

    removed = []
    for line in ACUTESIM_SOURCES_TO_REMOVE:
        if line in block:
            # Remove all occurrences within the block (there are duplicate quick_analysis.cpp)
            count = block.count(line)
            block = block.replace(line, "")
            removed.append(f"  removed ({count}x): {line.strip()}")
        else:
            print(f"  [WARN] Step B: line not found in block: {line.strip()}")

    content = content[:start_idx] + block + content[block_end:]
    report("Step B: Remove compute sources from ACUTESIM_SOURCES", original_block != block)
    for r in removed:
        print(r)
    return content


# ─────────────────────────────────────────────────────────────────────────────
# STEP C: Remove physics/compute include dirs from AcuteSim target_include_directories
# ─────────────────────────────────────────────────────────────────────────────

INCLUDE_DIRS_TO_REMOVE = [
    "    ${CMAKE_CURRENT_SOURCE_DIR}/physics\n",
    "    ${CMAKE_CURRENT_SOURCE_DIR}/compute\n",
    "    ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers\n",
    "    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math\n",
    "    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors\n",
    "    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure\n",
]

def step_c(content):
    # Find the target_include_directories for ${PROJECT_NAME}
    anchor = "target_include_directories(${PROJECT_NAME} PUBLIC\n"
    start_idx = content.find(anchor)
    if start_idx == -1:
        report("Step C: Locate target_include_directories(PROJECT_NAME)", False)
        return content

    # Find the end of this block (first ")\n" after anchor)
    search_from = start_idx + len(anchor)
    end_idx = content.find("\n)\n", search_from)
    if end_idx == -1:
        report("Step C: Find closing ) of include dirs block", False)
        return content
    block_end = end_idx + 3

    block = content[start_idx:block_end]
    original_block = block

    for line in INCLUDE_DIRS_TO_REMOVE:
        if line in block:
            block = block.replace(line, "", 1)
        else:
            print(f"  [WARN] Step C: include dir line not found: {line.strip()}")

    content = content[:start_idx] + block + content[block_end:]
    report("Step C: Remove physics/compute include dirs from AcuteSim", original_block != block)
    return content


# ─────────────────────────────────────────────────────────────────────────────
# STEP D: Add link to acutesim_orchestration/acutesim_compute_core for AcuteSim
# ─────────────────────────────────────────────────────────────────────────────

STEP_D_OLD = """\
# Link Qt libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)"""

STEP_D_NEW = """\
# Link Qt libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
# Link compute/orchestration libraries (replaces direct source compilation)
if(NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PRIVATE acutesim_orchestration)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE acutesim_compute_core)
endif()
# Enforce GUI/physics boundary at compile time
target_compile_definitions(${PROJECT_NAME} PRIVATE ACUTESIM_GUI_BUILD)"""

def step_d(content):
    return replace_exact(content, STEP_D_OLD, STEP_D_NEW, "Step D: Add link to acutesim_orchestration for AcuteSim")


# ─────────────────────────────────────────────────────────────────────────────
# STEP E: Simplify test_regression
# ─────────────────────────────────────────────────────────────────────────────

STEP_E_REGRESSION_OLD = """\
add_executable(test_regression
    tests/regression_main.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
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
target_compile_features(test_regression PUBLIC cxx_std_17)"""

STEP_E_REGRESSION_NEW = """\
add_executable(test_regression tests/regression_main.cpp)
target_link_libraries(test_regression PRIVATE acutesim_compute_core)
target_include_directories(test_regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_regression PUBLIC cxx_std_17)"""

def step_e_regression(content):
    return replace_exact(content, STEP_E_REGRESSION_OLD, STEP_E_REGRESSION_NEW, "Step E: Simplify test_regression")


# test_sensitivity
STEP_E_SENSITIVITY_OLD = """\
add_executable(test_sensitivity
    tests/test_sensitivity.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
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
target_compile_features(test_sensitivity PUBLIC cxx_std_17)"""

STEP_E_SENSITIVITY_NEW = """\
add_executable(test_sensitivity tests/test_sensitivity.cpp)
target_link_libraries(test_sensitivity PRIVATE acutesim_compute_core)
target_include_directories(test_sensitivity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_sensitivity PUBLIC cxx_std_17)"""

def step_e_sensitivity(content):
    return replace_exact(content, STEP_E_SENSITIVITY_OLD, STEP_E_SENSITIVITY_NEW, "Step E: Simplify test_sensitivity")


# test_superposition
STEP_E_SUPERPOSITION_OLD = """\
add_executable(test_superposition
    tests/test_superposition.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
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
target_compile_features(test_superposition PUBLIC cxx_std_17)"""

STEP_E_SUPERPOSITION_NEW = """\
add_executable(test_superposition tests/test_superposition.cpp)
target_link_libraries(test_superposition PRIVATE acutesim_compute_core)
target_include_directories(test_superposition PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_superposition PUBLIC cxx_std_17)"""

def step_e_superposition(content):
    return replace_exact(content, STEP_E_SUPERPOSITION_OLD, STEP_E_SUPERPOSITION_NEW, "Step E: Simplify test_superposition")


# test_mode_ladder
STEP_E_MODE_LADDER_OLD = """\
add_executable(test_mode_ladder
    tests/test_mode_ladder.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
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
target_compile_features(test_mode_ladder PUBLIC cxx_std_17)"""

STEP_E_MODE_LADDER_NEW = """\
add_executable(test_mode_ladder tests/test_mode_ladder.cpp)
target_link_libraries(test_mode_ladder PRIVATE acutesim_compute_core)
target_include_directories(test_mode_ladder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_mode_ladder PUBLIC cxx_std_17)"""

def step_e_mode_ladder(content):
    return replace_exact(content, STEP_E_MODE_LADDER_OLD, STEP_E_MODE_LADDER_NEW, "Step E: Simplify test_mode_ladder")


# test_erc_rules
STEP_E_ERC_OLD = """\
add_executable(test_erc_rules
    tests/test_erc_rules.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
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
target_compile_features(test_erc_rules PUBLIC cxx_std_17)"""

STEP_E_ERC_NEW = """\
add_executable(test_erc_rules tests/test_erc_rules.cpp)
target_link_libraries(test_erc_rules PRIVATE acutesim_compute_core)
target_include_directories(test_erc_rules PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_erc_rules PUBLIC cxx_std_17)"""

def step_e_erc(content):
    return replace_exact(content, STEP_E_ERC_OLD, STEP_E_ERC_NEW, "Step E: Simplify test_erc_rules")


# test_determinism
STEP_E_DETERMINISM_OLD = """\
add_executable(test_determinism
    tests/test_determinism.cpp
    physics/quick_analysis.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
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
add_test(NAME test_determinism COMMAND test_determinism)"""

STEP_E_DETERMINISM_NEW = """\
add_executable(test_determinism tests/test_determinism.cpp)
target_link_libraries(test_determinism PRIVATE acutesim_compute_core)
target_include_directories(test_determinism PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_determinism PUBLIC cxx_std_17)
add_test(NAME test_determinism COMMAND test_determinism)"""

def step_e_determinism(content):
    return replace_exact(content, STEP_E_DETERMINISM_OLD, STEP_E_DETERMINISM_NEW, "Step E: Simplify test_determinism")


# test_verification_harness
STEP_E_VERIFICATION_OLD = """\
add_executable(test_verification_harness
    tests/verification_main.cpp
    physics/quick_analysis.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
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
target_link_libraries(test_verification_harness PRIVATE Qt6::Core)"""

STEP_E_VERIFICATION_NEW = """\
add_executable(test_verification_harness tests/verification_main.cpp)
target_link_libraries(test_verification_harness PRIVATE acutesim_compute_core)
target_include_directories(test_verification_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_verification_harness PUBLIC cxx_std_17)"""

def step_e_verification(content):
    return replace_exact(content, STEP_E_VERIFICATION_OLD, STEP_E_VERIFICATION_NEW, "Step E: Simplify test_verification_harness")


# test_simulation
STEP_E_SIMULATION_OLD = """\
add_executable(test_simulation
    tests/simulation_test_main.cpp
    physics/quick_analysis.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
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
target_link_libraries(test_simulation PRIVATE Qt6::Core)"""

STEP_E_SIMULATION_NEW = """\
add_executable(test_simulation tests/simulation_test_main.cpp)
target_link_libraries(test_simulation PRIVATE acutesim_compute_core)
target_include_directories(test_simulation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_simulation PUBLIC cxx_std_17)"""

def step_e_simulation(content):
    return replace_exact(content, STEP_E_SIMULATION_OLD, STEP_E_SIMULATION_NEW, "Step E: Simplify test_simulation")


# verify_bjt_thermal
STEP_E_BJT_OLD = """\
add_executable(verify_bjt_thermal
    tests/verify_bjt_thermal.cpp
    physics/quick_analysis.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
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
target_link_libraries(verify_bjt_thermal PRIVATE Qt6::Core)"""

STEP_E_BJT_NEW = """\
add_executable(verify_bjt_thermal tests/verify_bjt_thermal.cpp)
target_link_libraries(verify_bjt_thermal PRIVATE acutesim_compute_core)
target_include_directories(verify_bjt_thermal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(verify_bjt_thermal PUBLIC cxx_std_17)"""

def step_e_bjt(content):
    return replace_exact(content, STEP_E_BJT_OLD, STEP_E_BJT_NEW, "Step E: Simplify verify_bjt_thermal")


# test_std_cells
STEP_E_STDCELLS_OLD = """\
add_executable(test_std_cells
    tests/test_std_cells.cpp
    physics/quick_analysis.cpp
    components/standard_cells.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
    compute/solvers/superposition.cpp
    compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
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
target_link_libraries(test_std_cells PRIVATE Qt6::Core)"""

STEP_E_STDCELLS_NEW = """\
add_executable(test_std_cells
    tests/test_std_cells.cpp
    components/standard_cells.cpp
)
target_link_libraries(test_std_cells PRIVATE acutesim_compute_core)
target_include_directories(test_std_cells PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_features(test_std_cells PUBLIC cxx_std_17)"""

def step_e_stdcells(content):
    return replace_exact(content, STEP_E_STDCELLS_OLD, STEP_E_STDCELLS_NEW, "Step E: Simplify test_std_cells")


# ─────────────────────────────────────────────────────────────────────────────
# STEP F: Simplify test_hierarchy_headless
# ─────────────────────────────────────────────────────────────────────────────

STEP_F_OLD = """\
add_executable(test_hierarchy_headless
    tests/test_hierarchy_headless.cpp
    components/design_db.cpp
    compute/infrastructure/netlist_compiler.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
)
target_include_directories(test_hierarchy_headless PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/components
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
)
target_link_libraries(test_hierarchy_headless PRIVATE Qt6::Core)
target_compile_features(test_hierarchy_headless PUBLIC cxx_std_17)
add_test(NAME test_hierarchy_headless COMMAND test_hierarchy_headless)
set_tests_properties(test_hierarchy_headless PROPERTIES LABELS "quick")"""

STEP_F_NEW = """\
add_executable(test_hierarchy_headless
    tests/test_hierarchy_headless.cpp
    components/design_db.cpp
)
target_link_libraries(test_hierarchy_headless PRIVATE acutesim_compute_core)
target_compile_features(test_hierarchy_headless PUBLIC cxx_std_17)
add_test(NAME test_hierarchy_headless COMMAND test_hierarchy_headless)
set_tests_properties(test_hierarchy_headless PROPERTIES LABELS "quick")"""

def step_f(content):
    return replace_exact(content, STEP_F_OLD, STEP_F_NEW, "Step F: Simplify test_hierarchy_headless")


# ─────────────────────────────────────────────────────────────────────────────
# STEP G: Simplify test_kernel_dispatch
# ─────────────────────────────────────────────────────────────────────────────

STEP_G_OLD = """\
add_executable(test_kernel_dispatch
    tests/test_kernel_dispatch.cpp
    compute/tensors/kernel_dispatch.cpp
    compute/tensors/physics_tensors.cpp
    compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
)
target_include_directories(test_kernel_dispatch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
    ${CMAKE_CURRENT_SOURCE_DIR}/components
)
target_link_libraries(test_kernel_dispatch PRIVATE Qt6::Core)
target_compile_features(test_kernel_dispatch PUBLIC cxx_std_17)
add_test(NAME test_kernel_dispatch COMMAND test_kernel_dispatch)"""

STEP_G_NEW = """\
add_executable(test_kernel_dispatch tests/test_kernel_dispatch.cpp)
target_link_libraries(test_kernel_dispatch PRIVATE acutesim_compute_core)
target_compile_features(test_kernel_dispatch PUBLIC cxx_std_17)
add_test(NAME test_kernel_dispatch COMMAND test_kernel_dispatch)"""

def step_g(content):
    return replace_exact(content, STEP_G_OLD, STEP_G_NEW, "Step G: Simplify test_kernel_dispatch")


# ─────────────────────────────────────────────────────────────────────────────
# STEP H: Simplify test_ai_simulation_intelligence
# ─────────────────────────────────────────────────────────────────────────────

STEP_H_OLD = """\
    add_executable(test_ai_simulation_intelligence
        tests/test_ai_simulation_intelligence.cpp
        ai_api/simulation/simulation_trace.cpp
        ai_api/simulation/speculative_runner.cpp
        ai_api/analysis/failure_classifier.cpp
        compute/solvers/circuitsim_solver.cpp
    compute/infrastructure/netlist_compiler.cpp
        compute/solvers/superposition.cpp
        compute/tensors/physics_tensors.cpp
        compute/tensors/kernel_dispatch.cpp
        compute/tensors/physics_kernels_avx512.cpp
    compute/tensors/physics_kernels_avx2.cpp
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
    target_compile_features(test_ai_simulation_intelligence PUBLIC cxx_std_17)"""

STEP_H_NEW = """\
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
    target_compile_features(test_ai_simulation_intelligence PUBLIC cxx_std_17)"""

def step_h(content):
    return replace_exact(content, STEP_H_OLD, STEP_H_NEW, "Step H: Simplify test_ai_simulation_intelligence")


# ─────────────────────────────────────────────────────────────────────────────
# STEP I: Simplify test_virtuoso_parity
# ─────────────────────────────────────────────────────────────────────────────

STEP_I_OLD = """\
    add_executable(test_virtuoso_parity
        tests/test_virtuoso_parity.cpp
        compute/orchestration/simulation_context.cpp
        compute/orchestration/batch_runner.cpp
        compute/infrastructure/postproc_engine.cpp
        compute/infrastructure/netlist_compiler.cpp
        compute/infrastructure/netlist_compiler_ctx.cpp
        compute/tensors/physics_tensors.cpp
        compute/tensors/kernel_dispatch.cpp
        compute/tensors/physics_kernels_avx512.cpp
        compute/tensors/physics_kernels_avx2.cpp
        compute/solvers/superposition.cpp
        compute/solvers/circuitsim_solver.cpp
    )
    target_include_directories(test_virtuoso_parity PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/physics
        ${CMAKE_CURRENT_SOURCE_DIR}/compute
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/math
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure
        ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
        ${CMAKE_CURRENT_SOURCE_DIR}/components
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(test_virtuoso_parity PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent)
    target_compile_features(test_virtuoso_parity PUBLIC cxx_std_17)"""

STEP_I_NEW = """\
    add_executable(test_virtuoso_parity tests/test_virtuoso_parity.cpp)
    target_link_libraries(test_virtuoso_parity PRIVATE acutesim_orchestration Qt6::Gui Qt6::Widgets Qt6::Concurrent)
    target_include_directories(test_virtuoso_parity PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_features(test_virtuoso_parity PUBLIC cxx_std_17)"""

def step_i(content):
    return replace_exact(content, STEP_I_OLD, STEP_I_NEW, "Step I: Simplify test_virtuoso_parity")


# ─────────────────────────────────────────────────────────────────────────────
# STEP J: Simplify test_eda_drivers
# ─────────────────────────────────────────────────────────────────────────────

STEP_J_OLD = """\
add_executable(test_eda_drivers
    tests/test_eda_drivers.cpp
    compute/orchestration/drivers/yosys_task.cpp
    compute/orchestration/drivers/yosys_log_parser.cpp
    compute/orchestration/drivers/openroad_task.cpp
    compute/orchestration/drivers/openroad_report_parser.cpp
    compute/orchestration/drivers/klayout_report_parser.cpp
    compute/orchestration/drivers/klayout_task.cpp
    compute/orchestration/drivers/tool_driver.cpp
    compute/orchestration/server/subprocess.cpp
)
target_include_directories(test_eda_drivers PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/compute
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/compute/orchestration/server
)
target_link_libraries(test_eda_drivers PRIVATE Qt6::Core)
target_compile_features(test_eda_drivers PUBLIC cxx_std_17)"""

STEP_J_NEW = """\
add_executable(test_eda_drivers tests/test_eda_drivers.cpp)
target_link_libraries(test_eda_drivers PRIVATE acutesim_orchestration)
target_compile_features(test_eda_drivers PUBLIC cxx_std_17)"""

def step_j(content):
    return replace_exact(content, STEP_J_OLD, STEP_J_NEW, "Step J: Simplify test_eda_drivers")


# ─────────────────────────────────────────────────────────────────────────────
# STEP K: Update test_hierarchy_gui to link acutesim_orchestration
# ─────────────────────────────────────────────────────────────────────────────

STEP_K_OLD = """\
    target_link_libraries(test_hierarchy_gui PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Svg
        ${TCL_LIBRARY}
    )"""

STEP_K_NEW = """\
    target_link_libraries(test_hierarchy_gui PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Svg
        ${TCL_LIBRARY}
        acutesim_orchestration
    )"""

def step_k(content):
    return replace_exact(content, STEP_K_OLD, STEP_K_NEW, "Step K: Add acutesim_orchestration to test_hierarchy_gui")


# ─────────────────────────────────────────────────────────────────────────────
# STEP L: Add test_headless_orchestrator BEFORE # ── CTest Labels
# ─────────────────────────────────────────────────────────────────────────────

HEADLESS_ORCHESTRATOR_BLOCK = """\
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

"""

STEP_L_ANCHOR = "# ── CTest Labels: quick (< 5s unit tests) vs heavy (GUI/stress/regression) ──"

def step_l(content):
    idx = content.find(STEP_L_ANCHOR)
    if idx == -1:
        report("Step L: Add test_headless_orchestrator", False, STEP_L_ANCHOR)
        return content
    new_content = content[:idx] + HEADLESS_ORCHESTRATOR_BLOCK + content[idx:]
    report("Step L: Add test_headless_orchestrator", True)
    return new_content


# ─────────────────────────────────────────────────────────────────────────────
# STEP M: Simplify full-stack tests (test_auto_harness, test_copilot_validate,
#         test_pdk_compliance, test_hierarchical_sim, test_introspection)
# Remove compute/* and compute/orchestration/* source lines from each.
# Then add target_link_libraries(test_X PRIVATE acutesim_orchestration) after
# the add_executable closing ).
# ─────────────────────────────────────────────────────────────────────────────

# Lines to remove from the full-stack test source lists (compute/* sources).
# Note these tests have mixed indentation (8 spaces or 4 spaces).
COMPUTE_SOURCES_PATTERN = [
    "        compute/tensors/physics_tensors.cpp\n",
    "        compute/tensors/kernel_dispatch.cpp\n",
    "        compute/tensors/physics_kernels_avx512.cpp\n",
    "    compute/tensors/physics_kernels_avx2.cpp\n",
    "        compute/solvers/superposition.cpp\n",
    "        compute/solvers/circuitsim_solver.cpp\n",
    "    compute/infrastructure/netlist_compiler.cpp\n",
    "        compute/orchestration/simulation_context.cpp\n",
    "        compute/orchestration/batch_runner.cpp\n",
    "        compute/infrastructure/netlist_compiler_ctx.cpp\n",
    "        gui/analysis/simulation_driver_adapter.h\n",
    "        compute/orchestration/driver/local_worker_driver.cpp\n",
    "        compute/infrastructure/postproc_engine.cpp\n",
    "        compute/orchestration/drivers/yosys_task.cpp\n",
    "        compute/orchestration/drivers/openroad_task.cpp\n",
    "        compute/orchestration/drivers/klayout_task.cpp\n",
    "        compute/orchestration/drivers/yosys_log_parser.cpp\n",
    "        compute/orchestration/drivers/openroad_report_parser.cpp\n",
    "        compute/orchestration/drivers/klayout_report_parser.cpp\n",
    "        compute/orchestration/drivers/tool_driver.cpp\n",
    "        compute/orchestration/server/subprocess.cpp\n",
    "        physics/quick_analysis.cpp\n",
    # Also handle physics/quick_analysis.cpp that appears at end of these big lists
]

# Include dirs to remove from target_include_directories of full-stack tests
COMPUTE_INCLUDE_DIRS_PATTERN = [
    "        ${CMAKE_CURRENT_SOURCE_DIR}/physics\n",
    "        ${CMAKE_CURRENT_SOURCE_DIR}/compute\n",
    "        ${CMAKE_CURRENT_SOURCE_DIR}/compute/solvers\n",
    "        ${CMAKE_CURRENT_SOURCE_DIR}/compute/math\n",
    "        ${CMAKE_CURRENT_SOURCE_DIR}/compute/tensors\n",
    "        ${CMAKE_CURRENT_SOURCE_DIR}/compute/infrastructure\n",
]


def remove_compute_sources_from_test(content, test_name, exe_start_pattern, exe_end_pattern):
    """
    Remove compute/* source lines from the add_executable block for test_name,
    then remove compute/* include dirs from the corresponding target_include_directories block,
    then add target_link_libraries(test_name PRIVATE acutesim_orchestration) after the exe block.
    """
    # Find the add_executable start
    start_idx = content.find(exe_start_pattern)
    if start_idx == -1:
        report(f"Step M: Locate add_executable for {test_name}", False, exe_start_pattern)
        return content

    # Find the closing ) of the add_executable
    close_paren_pattern = exe_end_pattern
    end_idx = content.find(close_paren_pattern, start_idx)
    if end_idx == -1:
        report(f"Step M: Locate closing ) for {test_name} add_executable", False)
        return content
    end_of_exe = end_idx + len(close_paren_pattern)

    exe_block = content[start_idx:end_of_exe]
    original_exe_block = exe_block

    # Remove compute sources
    for line in COMPUTE_SOURCES_PATTERN:
        exe_block = exe_block.replace(line, "")

    # Now find the target_include_directories block for this test
    inc_anchor = f"    target_include_directories({test_name} PRIVATE\n"
    inc_start = content.find(inc_anchor, end_of_exe)
    if inc_start == -1:
        # Try without leading spaces (for tests not inside if block)
        inc_anchor = f"target_include_directories({test_name} PRIVATE\n"
        inc_start = content.find(inc_anchor, end_of_exe)

    if inc_start != -1:
        inc_end = content.find("\n    )\n", inc_start)
        if inc_end == -1:
            inc_end = content.find("\n)\n", inc_start)
        if inc_end != -1:
            inc_end += len("\n    )\n") if content[inc_end:inc_end+6] == "\n    )" else len("\n)\n")
            inc_block = content[inc_start:inc_end]
            original_inc_block = inc_block
            for line in COMPUTE_INCLUDE_DIRS_PATTERN:
                inc_block = inc_block.replace(line, "")
            if inc_block != original_inc_block:
                # Replace include block in content (offset adjusted after exe block change)
                # We need to apply changes carefully
                pass

    # Build the new content: exe block + add link libs line + rest
    link_insert = f"    target_link_libraries({test_name} PRIVATE acutesim_orchestration)\n"

    changed = exe_block != original_exe_block
    report(f"Step M: Remove compute sources from {test_name}", changed)

    # Reconstruct content with modified exe block
    new_content = content[:start_idx] + exe_block + content[end_of_exe:]

    # Now remove compute include dirs from include block (search again since offsets changed)
    inc_anchor_8 = f"    target_include_directories({test_name} PRIVATE\n"
    inc_anchor_0 = f"target_include_directories({test_name} PRIVATE\n"

    # Find in new_content
    search_start = start_idx + len(exe_block)
    inc_start = new_content.find(inc_anchor_8, search_start)
    leading = "    "
    if inc_start == -1:
        inc_start = new_content.find(inc_anchor_0, search_start)
        leading = ""

    if inc_start != -1:
        # Find closing ) - it could be "    )\n" or ")\n"
        close8 = new_content.find("\n    )\n", inc_start)
        close0 = new_content.find("\n)\n", inc_start)
        if close8 != -1 and (close0 == -1 or close8 < close0):
            inc_close = close8 + len("\n    )\n")
        elif close0 != -1:
            inc_close = close0 + len("\n)\n")
        else:
            inc_close = -1

        if inc_close != -1:
            inc_block = new_content[inc_start:inc_close]
            original_inc_block = inc_block
            for line in COMPUTE_INCLUDE_DIRS_PATTERN:
                inc_block = inc_block.replace(line, "")
            if inc_block != original_inc_block:
                report(f"Step M: Remove compute include dirs from {test_name}", True)
                new_content = new_content[:inc_start] + inc_block + new_content[inc_close:]

    # Insert acutesim_orchestration link right after the modified exe closing )
    # Find the closing ) again in new_content
    end_idx2 = new_content.find(close_paren_pattern, start_idx)
    if end_idx2 != -1:
        end_of_exe2 = end_idx2 + len(close_paren_pattern)
        # Check if link line already present
        if f"target_link_libraries({test_name} PRIVATE acutesim_orchestration)" not in new_content[end_of_exe2:end_of_exe2+200]:
            new_content = new_content[:end_of_exe2] + link_insert + new_content[end_of_exe2:]
            report(f"Step M: Insert acutesim_orchestration link for {test_name}", True)

    return new_content


def step_m_auto_harness(content):
    return remove_compute_sources_from_test(
        content,
        "test_auto_harness",
        "    add_executable(test_auto_harness\n",
        "    )\n    target_include_directories(test_auto_harness"
    )


def step_m_copilot(content):
    return remove_compute_sources_from_test(
        content,
        "test_copilot_validate",
        "    add_executable(test_copilot_validate\n",
        "    )\n    target_include_directories(test_copilot_validate"
    )


def step_m_pdk_compliance(content):
    return remove_compute_sources_from_test(
        content,
        "test_pdk_compliance",
        "    add_executable(test_pdk_compliance\n",
        "    )\n    target_include_directories(test_pdk_compliance"
    )


def step_m_hierarchical_sim(content):
    return remove_compute_sources_from_test(
        content,
        "test_hierarchical_sim",
        "    add_executable(test_hierarchical_sim\n",
        "    )\n    target_include_directories(test_hierarchical_sim"
    )


def step_m_introspection(content):
    return remove_compute_sources_from_test(
        content,
        "test_introspection",
        "    add_executable(test_introspection\n",
        "    )\n    target_include_directories(test_introspection"
    )


# ─────────────────────────────────────────────────────────────────────────────
# ALSO: Remove compute/orchestration/driver/local_worker_driver.h from ACUTESIM_SOURCES
# (the .h files that got missed above, and the dto header)
# ─────────────────────────────────────────────────────────────────────────────

def step_b_extra(content):
    """Remove remaining header-only orchestration lines from ACUTESIM_SOURCES block."""
    start_marker = "set(ACUTESIM_SOURCES\n"
    start_idx = content.find(start_marker)
    if start_idx == -1:
        return content

    end_idx = content.find("\n)\n", start_idx + len(start_marker))
    if end_idx == -1:
        return content
    block_end = end_idx + 3

    block = content[start_idx:block_end]
    extra_lines = [
        "    compute/orchestration/dto/simulation_dto.h\n",
        "    compute/orchestration/driver/isimulation_driver.h\n",
        "    compute/orchestration/driver/local_worker_driver.h\n",
    ]
    original = block
    for line in extra_lines:
        block = block.replace(line, "")
    if block != original:
        report("Step B-extra: Remove orchestration header lines from ACUTESIM_SOURCES", True)
    content = content[:start_idx] + block + content[block_end:]
    return content


# ─────────────────────────────────────────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────────────────────────────────────────

def main():
    print(f"Reading {CMAKE_PATH} ...")
    content = read_file(CMAKE_PATH)
    original_len = len(content)
    print(f"  File size: {original_len} bytes, {content.count(chr(10))} lines\n")

    print("=== Step A: Insert library definitions ===")
    content, _ = step_a(content)

    print("\n=== Step B: Remove sources from ACUTESIM_SOURCES ===")
    content = step_b(content)
    content = step_b_extra(content)

    print("\n=== Step C: Remove physics/compute include dirs from AcuteSim ===")
    content = step_c(content)

    print("\n=== Step D: Add library links to AcuteSim executable ===")
    content, _ = step_d(content)

    print("\n=== Step E: Simplify pure-physics test targets ===")
    content, _ = step_e_regression(content)
    content, _ = step_e_sensitivity(content)
    content, _ = step_e_superposition(content)
    content, _ = step_e_mode_ladder(content)
    content, _ = step_e_erc(content)
    content, _ = step_e_determinism(content)
    content, _ = step_e_verification(content)
    content, _ = step_e_simulation(content)
    content, _ = step_e_bjt(content)
    content, _ = step_e_stdcells(content)

    print("\n=== Step F: Simplify test_hierarchy_headless ===")
    content, _ = step_f(content)

    print("\n=== Step G: Simplify test_kernel_dispatch ===")
    content, _ = step_g(content)

    print("\n=== Step H: Simplify test_ai_simulation_intelligence ===")
    content, _ = step_h(content)

    print("\n=== Step I: Simplify test_virtuoso_parity ===")
    content, _ = step_i(content)

    print("\n=== Step J: Simplify test_eda_drivers ===")
    content, _ = step_j(content)

    print("\n=== Step K: Add acutesim_orchestration to test_hierarchy_gui ===")
    content, _ = step_k(content)

    print("\n=== Step L: Add test_headless_orchestrator ===")
    content = step_l(content)

    print("\n=== Step M: Simplify full-stack tests ===")
    content = step_m_auto_harness(content)
    content = step_m_copilot(content)
    content = step_m_pdk_compliance(content)
    content = step_m_hierarchical_sim(content)
    content = step_m_introspection(content)

    print(f"\nWriting {CMAKE_PATH} ...")
    write_file(CMAKE_PATH, content)
    new_len = len(content)
    print(f"  Done. New size: {new_len} bytes ({new_len - original_len:+d}), {content.count(chr(10))} lines")
    print("\nAll steps complete.")


if __name__ == "__main__":
    main()
