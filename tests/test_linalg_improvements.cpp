#include <cstdint>
#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include <map>
#include <algorithm>
#include <thread>
#include "../compute/math/linalg.h"
#include "../compute/math/graph_utils.h"

// Simple test runner helper
#define TEST_CASE(name) void name(); \
    struct name##_Runner { \
        name##_Runner() { \
            std::cout << "[RUNNING] " << #name << "..." << std::endl; \
            name(); \
            std::cout << "[PASS] " << #name << std::endl; \
        } \
    } name##_instance; \
    void name()

// Helper to print matrix
void printMatrix(const Csr_matrix& m) {
    std::cout << "Rows: " << m.rows << ", Cols: " << m.cols << ", NNZ: " << m.nnz << "\n";
    for(int i=0; i<m.rows; ++i) {
        std::cout << "R" << i << ": ";
        for(int j=m.row_pointer[i]; j<m.row_pointer[i+1]; ++j) {
            std::cout << "(" << m.col_indices[j] << ", " << m.values[j] << ") ";
        }
        std::cout << "\n";
    }
}

TEST_CASE(Test_SoA_Parity) {
    MatrixConstructor mc;
    mc.setDimensions(3, 3);
    
    // Add triplets out of order
    mc.add(0, 0, 1.0);
    mc.add(2, 2, 5.0);
    mc.add(1, 1, 2.0);
    mc.add(0, 1, 0.5);
    mc.add(1, 0, 0.5);
    
    // Duplicate addition (should sum)
    mc.add(2, 2, 1.0); // Total (2,2) should be 6.0
    
    Csr_matrix m = mc.createCsr();
    
    assert(m.rows == 3);
    assert(m.nnz == 5);
    
    // Check specific values
    auto getVal = [&](int r, int c) {
        for(int j=m.row_pointer[r]; j<m.row_pointer[r+1]; ++j) {
            if(m.col_indices[j] == c) return m.values[j];
        }
        return 0.0;
    };
    
    assert(std::abs(getVal(0,0) - 1.0) < 1e-9);
    assert(std::abs(getVal(2,2) - 6.0) < 1e-9);
    assert(std::abs(getVal(1,0) - 0.5) < 1e-9);
}

TEST_CASE(Test_Batch_Add) {
    MatrixConstructor mc;
    mc.setDimensions(4, 4);
    
    std::vector<int> rows = {0, 1, 2, 3};
    std::vector<int> cols = {0, 1, 2, 3};
    std::vector<double> vals = {10.0, 20.0, 30.0, 40.0};
    
    mc.addBatch(rows, cols, vals);
    
    Csr_matrix m = mc.createCsr();
    assert(m.nnz == 4);
    assert(m.row_pointer[4] == 4);
}

TEST_CASE(Test_Checksum_Determinism) {
    MatrixConstructor mc1;
    mc1.setDimensions(2, 2);
    mc1.add(0, 0, 1.0);
    mc1.add(1, 1, 2.0);
    mc1.createCsr(); // Triggers checksum calc
    
    uint64_t c1 = mc1.getChecksum();
    
    MatrixConstructor mc2;
    mc2.setDimensions(2, 2);
    mc2.add(1, 1, 2.0); // Different order
    mc2.add(0, 0, 1.0);
    mc2.createCsr();
    
    uint64_t c2 = mc2.getChecksum();
    
    assert(c1 == c2 && "Checksum should be order independent");
    assert(c1 != 0 && "Checksum should not be zero");
    
    MatrixConstructor mc3;
    mc3.setDimensions(2, 2);
    mc3.add(0, 0, 1.0000000001); // Slight change
    mc3.add(1, 1, 2.0);
    mc3.createCsr();
    
    assert(mc3.getChecksum() != c1 && "Checksum should detect value changes");
}

TEST_CASE(Test_NodeOrdering) {
    // Create a matrix with high degree node 0
    // Node 0 connected to 1, 2, 3, 4
    // Node 1 connected to 2
    // Node 3 isolated
    
    MatrixConstructor mc;
    mc.setDimensions(5, 5);
    
    // Degree of 0: 4 + self = 5? (0,1), (0,2), (0,3), (0,4)
    mc.add(0, 1, 1); mc.add(1, 0, 1);
    mc.add(0, 2, 1); mc.add(2, 0, 1);
    mc.add(0, 3, 1); mc.add(3, 0, 1);
    mc.add(0, 4, 1); mc.add(4, 0, 1);
    
    // Degree of 1: connected to 0 and 2. Degree 2.
    mc.add(1, 2, 1); mc.add(2, 1, 1);
    
    Csr_matrix m = mc.createCsr();
    
    // degree(0) = 4 connections
    // degree(1) = 2 (0, 2)
    // degree(2) = 2 (0, 1)
    // degree(3) = 1 (0) 
    // degree(4) = 1 (0)
    
    // Expected sort order (ascending degree):
    // 3 or 4 (degree 1)
    // 4 or 3 (degree 1)
    // 1 or 2 (degree 2)
    // 2 or 1 (degree 2)
    // 0      (degree 4)
    
    std::vector<int> p = MathUtils::computeNodeOrdering(m);
    
    assert(p.size() == 5);
    
    // p[0] is the old node index that becomes new node 0.
    // We expect p[0] to be 3 or 4.
    
    // Let's verify degrees of the permuted order
    auto getDegree = [&](int oldNode) {
        return m.row_pointer[oldNode+1] - m.row_pointer[oldNode];
    };
    
    int prev_deg = -1;
    for(int i=0; i<5; ++i) {
        int oldNode = p[i];
        int deg = getDegree(oldNode);
        std::cout << "New Node " << i << " -> Old Node " << oldNode << " (Degree " << deg << ")\n";
        assert(deg >= prev_deg && "Ordering should be by ascending degree");
        prev_deg = deg;
    }
}

int main() {
    std::cout << "All MNA Improvement Tests Passed.\n";
    return 0;
}
