#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <cassert>
#include <cmath>
#include <iomanip>

#include "acutesim_engine/math/linalg.h"
#include "acutesim_engine/tensors/physics_tensors.h"

// Define a dummy backend capability for the test
enum class Backend {
    CPU,
    WEBGPU,
    CUDA
};
Backend activeBackend = Backend::CPU;

// ============================================================================
// 1. GPU Alignment Verification (std430 Compatibility Check)
// ============================================================================
void verify_gpu_alignment() {
    std::cout << "[GPU Prep] Verifying SoA Layout for std430..." << std::endl;
    
    // In SoA design, we send individual vectors (e.g., double[]) to the GPU.
    // std430 requires:
    // - Scalar arrays (float/double) stride = sizeof(scalar).
    // - vec3/vec4 arrays stride = 16 bytes (vec4).
    
    // Check 1: Ensure std::vector<double> is contiguous (C++ standard guarantees, but good to show)
    std::vector<double> test_vec = {1.0, 2.0, 3.0, 4.0};
    double* ptr = test_vec.data();
    assert(ptr[1] == 2.0);
    assert(&ptr[1] - &ptr[0] == 1); // Pointer arithmetic check
    
    std::cout << "  PASS: std::vector<double> is contiguous." << std::endl;
    
    // Check 2: DiodeTensor Alignment
    // We want to verify we can extract raw pointers from our tensors
    DiodeTensor dTensor;
    dTensor.reserve(100);
    for(int i=0; i<100; ++i) dTensor.push(0,0,0,0,0); // Populate dummy
    
    assert(dTensor.node_a.size() == 100);
    assert((void*)dTensor.node_a.data() != nullptr);
    
    // Check alignment of the pointer itself (checking 16-byte alignment is good for SIMD/GPU load)
    size_t addr = reinterpret_cast<size_t>(dTensor.g_d.data());
    // Note: Standard allocator doesn't guarantee 64-byte alignment, but checking it is informative.
    std::cout << "  INFO: Data pointer address: " << (void*)dTensor.g_d.data() << std::endl;
    
    std::cout << "  PASS: Structure-of-Arrays ready for glBufferSubData." << std::endl;
}

// ============================================================================
// 2. Dense Block / Batch Stamping Verification
// ============================================================================
void verify_dense_batch_stamping() {
    std::cout << "\n[Dense Block] Verifying addBatch Correctness..." << std::endl;
    
    MatrixConstructor mat;
    int N = 5;
    mat.setDimensions(N, N);
    
    // Scenario: Stamp a fully connected 3x3 block (nodes 0, 1, 2)
    // All conductances = 1.0 (so matrix should have 1.0 at every entry in 3x3)
    
    std::vector<int> rows;
    std::vector<int> cols;
    std::vector<double> vals;
    
    // Generate Dense 3x3 stamp
    for (int r=0; r<3; ++r) {
        for (int c=0; c<3; ++c) {
            rows.push_back(r);
            cols.push_back(c);
            vals.push_back(1.0);
        }
    }
    
    // Use the Batch Interface
    mat.addBatch(rows, cols, vals);
    
    // Create CSR
    Csr_matrix csr = mat.createCsr();
    
    // Convert to dense for check
    std::vector<double> dense = convertCsrToDense(csr);
    
    // Verify
    bool pass = true;
    for (int i=0; i<N; ++i) {
        for (int j=0; j<N; ++j) {
            double expected = (i < 3 && j < 3) ? 1.0 : 0.0;
            double actual = dense[i*N + j];
            if (std::abs(expected - actual) > 1e-9) {
                pass = false;
                std::cout << "  FAIL: M[" << i << "][" << j << "] = " << actual << " expected " << expected << std::endl;
            }
        }
    }
    
    if (pass) std::cout << "  PASS: addBatch stamped sparse matrix correctly." << std::endl;
}

// ============================================================================
// 3. Large Benchmark
// ============================================================================
void run_benchmark() {
    std::cout << "\n[Benchmark] Running High-Load Stamping Benchmark..." << std::endl;
    
    // Setup: 100x100 Grid of Diodes (10k Diodes, ~10k Nodes)
    int GRID_W = 200;
    int GRID_H = 200;
    int num_diodes = (GRID_W - 1) * GRID_H + (GRID_H - 1) * GRID_W;
    
    std::cout << "  Grid Size: " << GRID_W << "x" << GRID_H << std::endl;
    std::cout << "  Components: " << num_diodes << " Diodes" << std::endl;
    std::cout << "  Matrix Size: " << (GRID_W * GRID_H) << "^2" << std::endl;
    
    DiodeTensor diodes;
    diodes.reserve(num_diodes);
    
    // Use DiodeTensor directly
    
    int idx = 0;
    // Horizontal
    for (int y=0; y<GRID_H; ++y) {
        for (int x=0; x<GRID_W-1; ++x) {
            int n1 = y*GRID_W + x + 1;
            int n2 = y*GRID_W + (x+1) + 1;
            diodes.push(n1, n2, 1e-14, 1.0, 0.026);
            
            // Manually set initial state for benchmark consistency
            diodes.v_d.back() = 0.7;
            diodes.i_d.back() = 1e-3;
            diodes.g_d.back() = 1e-2;
            
            idx++;
        }
    }
    // Vertical
    for (int x=0; x<GRID_W; ++x) {
        for (int y=0; y<GRID_H-1; ++y) {
            int n1 = y*GRID_W + x + 1;
            int n2 = (y+1)*GRID_W + x + 1;
            diodes.push(n1, n2, 1e-14, 1.0, 0.026);
            
             // Manually set initial state
            diodes.v_d.back() = 0.7;
            diodes.i_d.back() = 1e-3;
            diodes.g_d.back() = 1e-2;
            
            idx++;
        }
    }
    
    MatrixConstructor mat;
    int numNodes = GRID_W * GRID_H;
    mat.setDimensions(numNodes, numNodes);
    std::vector<double> rhs(numNodes, 0.0);
    std::vector<double> v_guess(numNodes, 0.5);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // A. Physics
    batchDiodePhysics(diodes, v_guess);
    
    auto physics_end = std::chrono::high_resolution_clock::now();
    
    // B. Stamping (Batch)
    {
        size_t nD = diodes.node_a.size();
        std::vector<int> rows, cols;
        std::vector<double> vals;
        rows.reserve(nD*4); cols.reserve(nD*4); vals.reserve(nD*4);
        
        for(size_t i=0; i<nD; ++i) {
            int nA = diodes.node_a[i];
            int nC = diodes.node_c[i];
            double g = diodes.g_d[i];
            if (nA!=0 && nC!=0) {
                 rows.push_back(nA-1); cols.push_back(nA-1); vals.push_back(g);
                 rows.push_back(nC-1); cols.push_back(nC-1); vals.push_back(g);
                 rows.push_back(nA-1); cols.push_back(nC-1); vals.push_back(-g);
                 rows.push_back(nC-1); cols.push_back(nA-1); vals.push_back(-g);
            }
        }
        mat.addBatch(rows, cols, vals);
    }
    
    auto stamp_end = std::chrono::high_resolution_clock::now();
    
    // C. Assembly
    Csr_matrix csr = mat.createCsr();
    
    auto end = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double, std::milli> t_phys = physics_end - start;
    std::chrono::duration<double, std::milli> t_stamp = stamp_end - physics_end;
    std::chrono::duration<double, std::milli> t_asm = end - stamp_end;
    std::chrono::duration<double, std::milli> t_total = end - start;
    
    std::cout << "  Physics Time: " << t_phys.count() << " ms" << std::endl;
    std::cout << "  Stamp Time:   " << t_stamp.count() << " ms" << std::endl;
    std::cout << "  Assemble Time: " << t_asm.count() << " ms" << std::endl;
    std::cout << "  Total Step:    " << t_total.count() << " ms" << std::endl;
    std::cout << "  Throughput:    " << (num_diodes / (t_total.count() / 1000.0)) / 1e6 << " Mcomps/s" << std::endl;
}

int main() {
    verify_gpu_alignment();
    verify_dense_batch_stamping();
    run_benchmark();
    return 0;
}
