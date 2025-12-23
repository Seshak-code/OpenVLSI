#pragma once

#include <vector>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <omp.h>

/**
 * ==================================================================================
 * GUIDE: IMPLEMENTING A CPU-BASED SPARSE MATRIX SOLVER
 * ==================================================================================
 * 
 * This file serves as a step-by-step blueprint for building a high-performance
 * sparse linear algebra library on the CPU.
 * 
 * OBJECTIVE: Solve Ax = b efficiently when A is large and sparse (mostly zeros).
 */

// ==================================================================================
// STEP 1: DATA STRUCTURES - COMPRESSED SPARSE ROW (CSR)
// ==================================================================================
/*
 * The Standard Format: CSR
 * - 'values': The non-zero values (size N_nz)
 * - 'col_indices': The column index for each value (size N_nz)
 * - 'row_ptr': The index in 'values' where each row starts (size N + 1)
 * 
 * Accessing row i:
 *   Loop from k = row_ptr[i] to row_ptr[i+1] - 1.
 *   Value is values[k] at column col_indices[k].
 */

struct Csr_matrix {
    std::vector<double> values;
    std::vector<int> col_indices;
    std::vector<int> row_ptr;
    int rows;
    int cols;
    int nnz; // Number of non-zeros
};

// ==================================================================================
// STEP 2: MATRIX ASSEMBLY - THE "TRIPLET" METHOD
// ==================================================================================
/*
 * PROBLEM:
 * Inserting directly into CSR is O(N) because you have to shift arrays.
 * This is terrible for "stamping" components in a loop.
 * 
 * SOLUTION: Coordinate List (COO) / Triplet List
 * 1. Store every contribution as a tuple {row, col, value}. Allow duplicates! (e.g., two resistors connected to the same node).
 * 2. Once simulation setup is done, SORT and COMPRESS into CSR.
 * 
 * MATH:
 * A_ij = sum(all triplets where row==i and col==j)
 */

struct Triplet {
    int row;
    int col;
    double value;
};

// PSEUDO-CODE IMPLEMENTATION:
/*
class MatrixBuilder {
    vector<Triplet> triplets;

    void add(r, c, val) {
        triplets.push_back({r, c, val});
    }

        Csr_matrix buildCSR() {
        1. Sort triplets by row, then by column.
        2. Iterate through sorted triplets.
        3. If current triplet (r,c) == previous (r,c), sum the values.
        4. Else, push new value and column index to CSR arrays.
        5. Update row_ptr array to track where row 'r' begins.
        return csr;
    }
}
*/

class MatrixConstructor {
private:
    std::vector<Triplet> t;
    int numRows = 0;
    int numCols = 0;

public:
    void add(int r, int c, double val)
    {
        t.push_back({r, c, val});
        if (r >= numRows) 
            numRows = r + 1;
        if (c >= numCols) 
            numCols = c + 1;
    }

    Csr_matrix createCsr()
    {
        Csr_matrix m;
        m.rows = numRows;
        m.cols = numCols;

        if (t.empty()) {
            m.row_ptr.assign(numRows + 1, 0);
            return m;
        }

        // 1. Sort triplets (needed for duplicate merging and row-major order)
        std::sort(t.begin(), t.end(), [](const Triplet& first, const Triplet& last) 
        {
            return (first.row < last.row || (first.row == last.row && first.col < last.col));
        });

        // 2. Prepare row_ptr (Histogram counts)
        m.row_ptr.assign(numRows + 1, 0);

        // 3. Merge duplicates and fill values
        for(int i = 0; i < (int)t.size(); i++)
        {
            // If the current triplet is the same row/col as the previous one, sum the values.
            if(i > 0 && t[i].row == t[i - 1].row && t[i].col == t[i - 1].col)
            {
                if (!m.values.empty()) {
                    m.values.back() += t[i].value;
                }
            }
            else
            {
                // New unique cell: add value/col and increment row count
                m.values.push_back(t[i].value);
                m.col_indices.push_back(t[i].col);
                m.row_ptr[t[i].row + 1]++;
            }
        }

        // 4. Finalize row_ptr (Prefix Sum)
        // This converts "counts per row" into "starting indices" for the CSR format.
        for (int i = 0; i < numRows; ++i)
            m.row_ptr[i + 1] += m.row_ptr[i];

        m.nnz = (int)m.values.size();
        return m;
    }
};

// ==================================================================================
// STEP 3: SPARSE MATRIX-VECTOR MULTIPLICATION (SpMV)
// ==================================================================================
/*
 * IMPORTANCE:
 * This is the "inner loop" of any iterative solver. It consumes 90% of runtime.
 * 
 * MATH:
 * y = A * x
 * y_i = sum( A_ij * x_j ) for all non-zero A_ij in row i.
 *  
 * ACCESS PATTERN:
 * We iterate strictly through 'values' and 'col_indices' (streaming memory).
 */

// Optimized SpMV:
// 1. Pass by const reference to avoid expensive copying.
// 2. Use OpenMP for multi-threading.
inline void matrixMultiplication(const Csr_matrix& m, const double* x, double* y)
{
    // Use OpenMP for scaling across threads
    #pragma omp parallel for
    for(int i = 0; i < m.rows; i++)
    {
        double sum = 0.0;
        int start = m.row_ptr[i];
        int end   = m.row_ptr[i+1];
        
        for (int k = start; k < end; ++k) {
            sum += m.values[k] * x[ m.col_indices[k] ];
        }
        y[i] = sum;
    }
}
// ==================================================================================
// STEP 4: THE SOLVER - CONJUGATE GRADIENT (CG)
// ==================================================================================
/*
 * CONTEXT:
 * Direct solvers (LU) are hard to implement essentially for sparse matrices (fill-in).
 * Iterative solvers guess 'x', calculate error, and improve 'x'.
 * 
 * ALGORITHM: Conjugate Gradient
 * - Only works for Symmetric Positive Definite matrices (Resistive networks often are).
 * - Requires only matrix-vector multiplication (SpMV) and vector ops.
 * 
 * MATH:
 * We want to minimize Quadratic Form: f(x) = 0.5 * x^T A x - b^T x
 * The gradient is Ax - b (the residual).
 * We move along "conjugate" directions to reach the minimum efficiently.
 */

// PSEUDO-CODE IMPLEMENTATION:
/*
vector<double> solveCG(const CSRMatrix& A, const vector<double>& b) {
    int n = A.rows;
    vector<double> x(n, 0.0); // Initial guess
    vector<double> r = b;     // Residual r = b - Ax (since x=0)
    vector<double> p = r;     // Search direction
    double rsold = dot_product(r, r);

    for (int i = 0; i < n; ++i) { // Max iterations usually N
        vector<double> Ap(n);
        spmv(A, p, Ap);       // The expensive part!

        double alpha = rsold / dot_product(p, Ap);
        
        // Update x and r
        x = x + alpha * p;
        r = r - alpha * Ap;

        double rsnew = dot_product(r, r);
        if (sqrt(rsnew) < 1e-10) break; // Converged!

        // Compute new search direction
        p = r + (rsnew / rsold) * p;
        rsold = rsnew;
    }
    return x;
}
*/

// ==================================================================================
// SUMMARY FOR IMPLEMENTATION
// ==================================================================================
/*
 * 1. Create a `SparseMatrix` class that holds triplets during assembly.
 * 2. Add a `finalize()` method that converts triplets to CSR format.
 * 3. Implement helper functions:
 *    - `vector_add(a, b)`
 *    - `vector_scale(a, s)`
 *    - `dot_product(a, b)`
 * 4. Implement `spmv(matrix, vector)`
 * 5. Implement `solve_cg(matrix, rhs)` using the blocks above.
 * 
 * This approach is efficient, cache-friendly, and relatively simple to write
 * compared to a full LU decomposition for sparse matrices.
 */
