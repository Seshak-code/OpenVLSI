#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

/*
 * linalg.h
 * Sparse linear algebra library optimized for MNA-based circuit simulation
 * workloads.
 *
 * This library provides a complete toolchain for solving sparse linear systems
 * (Ax = b):
 * 1. CSR (Compressed Sparse Row) matrix storage format.
 * 2. Triplet-based matrix assembly with duplicate handling.
 * 3. OpenMP-parallelized Sparse Matrix-Vector Multiplication (SpMV).
 * 4. Preconditioned Conjugate Gradient (PCG) iterative solver.
 */

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/*
 * Compressed Sparse Row (CSR) Matrix
 *
 * The CSR format is designed for memory efficiency by storing only non-zero
 * elements. It consists of three primary arrays:
 * - values: Stores the numerical values of non-zero entries in row-major order.
 * - col_indices: Stores the column index corresponding to each stored value.
 * - row_pointer: Stores the index in 'values' where each row begins.
 *
 * This structure enables fast matrix-vector operations while significantly
 * reducing memory overhead compared to a dense (N x N) grid.
 */

struct Csr_matrix {
  std::vector<double> values;   // Non-zero numerical data
  std::vector<int> col_indices; // Column indices for each value
  std::vector<int> row_pointer; // Pointers to the start of each row
  int rows;                     // Dimension of the matrix (rows)
  int cols;                     // Dimension of the matrix (columns)
  int nnz;                      // Total count of non-zero entries
};

/*
 * Triplet - A single matrix entry in Coordinate (COO) format.
 *
 * A Triplet represents a raw, uncompressed entry defined by its
 * (row, column, value) coordinates. Triplets are used as an intermediate
 * stage during matrix assembly before compression into CSR format.
 */

struct Triplet {
  int row;
  int col;
  double value;
};

// ============================================================================
// MATRIX ASSEMBLY
// ============================================================================

/*
 * MatrixConstructor - Accumulator for building sparse matrices from individual
 * components.
 *
 * In circuit simulation, multiple components (resistors, sources) may connect
 * to the same nodes, contributing to the same matrix cell. This class collects
 * these individual "stamps" as Triplets and merges them during final
 * compression.
 */

#include <mutex>

class MatrixConstructor {
private:
  // LOCK-FREE ASSEMBLY (Phase 2 Tensor Backend)
  // Instead of a single mutex-protected vector, we use per-thread buffers.
  // This eliminates contention during parallel stamping.
  std::vector<std::vector<Triplet>> thread_triplets;

  int numRows = 0;
  int numCols = 0;

public:
  MatrixConstructor() {
      // Pre-allocate buffers for maximum potential threads
      int max_threads = 1;
      #ifdef _OPENMP
      max_threads = omp_get_max_threads();
      #endif
      thread_triplets.resize(max_threads);
  }

  // Resets the constructor for a new matrix assembly
  void clear() {
    // No locks needed if clear() is called sequentially between solves
    for(auto& buf : thread_triplets) {
        buf.clear();
    }
    numRows = 0;
    numCols = 0;
  }
  
  // Explicitly set matrix dimensions (prevents auto-grow beyond expected bounds)
  void setDimensions(int rows, int cols) {
    numRows = rows;
    numCols = cols;
  }

  /*
   * Stamps a numerical value at the specified (row, col) coordinate.
   * Lock-Free Implementation: Uses thread-local buffers.
   */
  void add(int row, int col, double value) {
    assert(row >= 0 && "Row index can't be negative");
    assert(col >= 0 && "Column index can't be negative");

    int tid = 0;
    #ifdef _OPENMP
    tid = omp_get_thread_num();
    #endif
    
    // Safety check for dynamic thread counts
    if (tid >= (int)thread_triplets.size()) {
        // Should not happen if constructed correctly, but specialized handling if OMP changes
        // Fallback or resize (unsafe in parallel)
        // Just force to 0 if out of bounds (safe fallback, albeit contested if we had a lock, but we don't)
        // Crash in debug
        assert(false && "Thread ID exceeds pre-allocated buffer size");
        tid = 0; 
    }

    thread_triplets[tid].push_back({row, col, value});

    // Updating dimensions is RACY.
    // However, setDimensions() is usually called beforehand.
    // We can use atomics for numRows/numCols if strict tracking is needed.
    // Or just relax and fix it in createCsr().
    // For MNA, we usually know N.
    // Let's use atomic update only if new max found (optimization: read first).
    if (row >= numRows) {
        #pragma omp atomic write
        numRows = row + 1;
    }
    if (col >= numCols) {
        #pragma omp atomic write
        numCols = col + 1;
    }
  }
  
  // Batch Stamping Interface (Phase 2)
  void addBatch(const std::vector<int>& rows, const std::vector<int>& cols, const std::vector<double>& values) {
      size_t n = rows.size();
      int tid = 0;
      #ifdef _OPENMP
      tid = omp_get_thread_num();
      #endif
      
      auto& buf = thread_triplets[tid];
      buf.reserve(buf.size() + n);
      
      for(size_t i=0; i<n; ++i) {
          buf.push_back({rows[i], cols[i], values[i]});
      }
  }

  /*
   * Converts uncompressed Triplets into the finalized CSR format.
   * Merges all thread buffers first.
   */
  Csr_matrix createCsr() {
    // 1. Merge Thread Buffers (Parallelized)
    // Calculate total size and offsets
    size_t num_threads = thread_triplets.size();
    std::vector<size_t> sizes(num_threads);
    std::vector<size_t> offsets(num_threads + 1, 0);
    
    // Serial scan for offsets (fast enough, negligible compared to copy)
    for (size_t i = 0; i < num_threads; ++i) {
        sizes[i] = thread_triplets[i].size();
        offsets[i + 1] = offsets[i] + sizes[i];
    }
    
    size_t total_triplets = offsets[num_threads];
    std::vector<Triplet> merged_triplets;
    merged_triplets.resize(total_triplets);
    
    // Parallel Copy
    #pragma omp parallel for
    for (size_t t = 0; t < num_threads; ++t) {
        if (sizes[t] > 0) {
            std::copy(thread_triplets[t].begin(), thread_triplets[t].end(),
                      merged_triplets.begin() + offsets[t]);
        }
    }

    // matrix object
    Csr_matrix matrix;

    matrix.rows = numRows;
    matrix.cols = numCols;

    if (merged_triplets.empty()) {
      matrix.row_pointer.assign(numRows + 1, 0);
      return matrix;
    }

    // Sorting to group same entries
    std::sort(merged_triplets.begin(), merged_triplets.end(),
              [](const Triplet &a, const Triplet &b) {
                return (a.row < b.row || (a.row == b.row && a.col < b.col));
              });

    // Initialize Row Pointer array
    matrix.row_pointer.assign(numRows + 1, 0);

    // Initial CSR population (Deduplication)
    const int numTriplets = (int)merged_triplets.size();
    
    // Reserve estimation (assume 20% duplicates)
    matrix.values.reserve(numTriplets);
    matrix.col_indices.reserve(numTriplets);

    for (int tripletIndex = 0; tripletIndex < numTriplets; tripletIndex++) {
      const Triplet &currentTriplet = merged_triplets[tripletIndex];

      if (currentTriplet.row >= numRows || currentTriplet.col >= numCols) {
           // Skip out of bounds (sanity)
           continue; 
      }

      // Check for duplicate coordinates matches previous valid entry
      if (tripletIndex > 0 &&
          currentTriplet.row == merged_triplets[tripletIndex - 1].row &&
          currentTriplet.col == merged_triplets[tripletIndex - 1].col) {
        if (!matrix.values.empty()) {
          matrix.values.back() += currentTriplet.value;
        }
      } else {
        matrix.values.push_back(currentTriplet.value);
        matrix.col_indices.push_back(currentTriplet.col);
        matrix.row_pointer[currentTriplet.row + 1]++;
      }
    }

    // Prefix Sum Transformation
    for (int rowIndex = 0; rowIndex < numRows; ++rowIndex)
      matrix.row_pointer[rowIndex + 1] += matrix.row_pointer[rowIndex];

    matrix.nnz = (int)matrix.values.size();

    assert(matrix.row_pointer[numRows] == matrix.nnz &&
           "Row pointer mismatch with NNZ");
    return matrix;
  }
};

// ============================================================================
// SPARSE MATRIX-VECTOR MULTIPLICATION (SpMV)
// ============================================================================

/*
 * Performs Sparse Matrix-Vector Multiplication (SpMV): y = A * x.
 *
 * For each row in the sparse matrix, we compute the dot product of that row
 * with the dense input vector. Only non-zero entries of 'A' are processed.
 * Parallelizes independent row dot-products using OpenMP.
 */

inline void matrixMultiplication(const Csr_matrix &matrix,
                                 const double *inputVector,
                                 double *outputVector) {
  // NASA Rule #7: Validate pointers and dimensions
  assert(inputVector != nullptr && "inputVector cannot be null");
  assert(outputVector != nullptr && "outputVector cannot be null");
  assert(matrix.rows >= 0 && matrix.cols >= 0 &&
         "Matrix dimensions must be not negative");

  // Runtime safety checks
  assert(matrix.row_pointer.size() >= (size_t)matrix.rows + 1 &&
         "Row pointer size mismatch");

#pragma omp parallel for
  for (int rowIndex = 0; rowIndex < matrix.rows; rowIndex++) {
    double rowSum = 0.0;

    // Define the traversal range for the current row
    int rowStart = matrix.row_pointer[rowIndex];
    int rowEnd = matrix.row_pointer[rowIndex + 1];

    assert(rowStart >= 0 && rowEnd <= (int)matrix.values.size() &&
           "Row bounds out of range");

    // Accumulate dot product: rowSum = sum(value[j] * inputVector[column[j]])
    for (int elementIndex = rowStart; elementIndex < rowEnd; ++elementIndex) {
      int col = matrix.col_indices[elementIndex];
      assert(col >= 0 && col < matrix.cols && "Column index out of range");
      rowSum += matrix.values[elementIndex] * inputVector[col];
    }

    // Store the final accumulation for this dimension
    outputVector[rowIndex] = rowSum;
  }
}

// ============================================================================
// VECTOR OPERATIONS (OpenMP Optimized)
// ============================================================================

// Computes the dot product of two vectors: sum = sum(A[i] * B[i])

inline double dotProduct(const std::vector<double> &vectorA,
                         const std::vector<double> &vectorB) {

  assert(vectorA.size() == vectorB.size() &&
         "Vectors must be of the same size");
  assert(!vectorA.empty() && "Vectors should not be empty");

  double sum = 0.0;
  const int n = (int)vectorA.size();
#pragma omp parallel for reduction(+ : sum)
  for (int index = 0; index < n; index++)
    sum += vectorA[index] * vectorB[index];
  return sum;
}

// Computes result = vectorA + scalar * vectorB (AXPY operation)

inline void vectorAdd(std::vector<double> &result,
                      const std::vector<double> &vectorA,
                      const std::vector<double> &vectorB, double scalar) {
  assert(vectorA.size() == vectorB.size() &&
         "Input vectors must be of same size");
  assert(result.size() == vectorA.size() &&
         "Result vector must match input size");

  const int n = (int)vectorA.size();
#pragma omp parallel for
  for (int index = 0; index < n; index++)
    result[index] = vectorA[index] + scalar * vectorB[index];

  assert(n >= 0 && "Vector size logic error");
}

/*
 * Extracts the inverse of the diagonal elements of a CSR matrix.
 *
 * This is used for the Jacobi preconditioner. If a diagonal element is zero or
 * near-zero, the inverse is set to 1.0, effectively disabling preconditioning
 * for that row to preserve numerical stability.
 *
 * Returns: A vector containing 1/A[i][i] for each row i.
 */

inline std::vector<double> getDiagonalInverse(const Csr_matrix &matrix) {
  assert(matrix.rows >= 0 && "Matrix rows cannot be negative");

  const double zeroThreshold = 1e-18;
  std::vector<double> diagonalInverse(matrix.rows, 1.0);

  // Pre-allocation check
  assert(diagonalInverse.size() == (size_t)matrix.rows &&
         "Vector pre-allocation error");

#pragma omp parallel for
  for (int rowIndex = 0; rowIndex < matrix.rows; rowIndex++) {
    const int rowStart = matrix.row_pointer[rowIndex];
    const int rowEnd = matrix.row_pointer[rowIndex + 1];

    for (int elementIndex = rowStart; elementIndex < rowEnd; elementIndex++) {
      if (matrix.col_indices[elementIndex] == rowIndex) {
        if (std::abs(matrix.values[elementIndex]) > zeroThreshold)
          diagonalInverse[rowIndex] = 1.0 / matrix.values[elementIndex];

        break; // If a diagonal is found, no need to check further in this row
      }
    }
  }
  return diagonalInverse;
}

// Contains the results of a linear solver execution.

struct SolverResult {
  std::vector<double> solution;
  double finalResidual;
  int iterations;
  bool converged;
};

/*
 * This solves Ax = b using the Preconditioned Conjugate Gradient (PCG) method.
 *
 * Uses a Jacobi (diagonal) preconditioner for faster convergence on diagonal
 * dominant matrices.
 *
 * matrix: The sparse matrix A (must be symmetric positive definite).
 * rhsVector: The right-hand side vector b.
 * tolerance: Convergence tolerance (stops when ||r|| < tolerance).
 * maxIterations: Maximum number of solver iterations.
 *
 * Returns: The solution vector, final residual, and iteration count.
 */

inline SolverResult solvePCG(const Csr_matrix &matrix,
                             const std::vector<double> &rhsVector,
                             double tolerance = 1e-10,
                             int maxIterations = 1000) {

  assert(matrix.rows > 0 && "Matrix must have at least one row");
  assert(matrix.rows == (int)rhsVector.size() &&
         "Matrix rows must match RHS vector size");
  assert(maxIterations > 0 && maxIterations < 100000 &&
         "maxIterations must be in [1, 100000)");
  assert(tolerance > 0 && "Tolerance must be positive");
  assert(matrix.cols == matrix.rows && "PCG requires a square matrix");

  const double divisionByZeroThreshold = 1e-20;
  const int numUnknowns = matrix.rows;

  std::vector<double> solution(numUnknowns, 0.0);
  std::vector<double> residual = rhsVector; // residual = b - A*x, with x=0

  assert(residual.size() == (size_t)numUnknowns &&
         "Residual vector size error");

  // Getting Jacobi Preconditioner (M^-1 = 1/diag(A))
  std::vector<double> preconditionerInverse = getDiagonalInverse(matrix);

  // Apply preconditioner: preconditionedResidual = M^-1 * residual
  std::vector<double> preconditionedResidual(numUnknowns);
  for (int i = 0; i < numUnknowns; i++) {
    preconditionedResidual[i] = residual[i] * preconditionerInverse[i];
  }

  std::vector<double> searchDirection = preconditionedResidual;
  double residualDotPreconditioned =
      dotProduct(residual, preconditionedResidual);

  // Early exit if already converged
  double initialResidualNorm = std::sqrt(dotProduct(residual, residual));
  if (initialResidualNorm < tolerance)
    return {solution, initialResidualNorm, 0, true};

  int iteration = 0;
  double currentResidualNorm = initialResidualNorm;
  const int maxIters = maxIterations;
  for (iteration = 0; iteration < maxIters; iteration++) {
    // matrixTimesSearchDirection = A * searchDirection
    std::vector<double> matrixTimesSearchDirection(numUnknowns);
    matrixMultiplication(matrix, searchDirection.data(),
                         matrixTimesSearchDirection.data());

    double searchDirectionDotAp =
        dotProduct(searchDirection, matrixTimesSearchDirection);
    if (std::abs(searchDirectionDotAp) < divisionByZeroThreshold) {
      // Numerical instability detected. Instead of crashing, return partial
      // result as failed.
      return {solution, currentResidualNorm, iteration, false};
    }

    double stepSize = residualDotPreconditioned / searchDirectionDotAp;

    // Update solution: x = x + stepSize * p
    vectorAdd(solution, solution, searchDirection, stepSize);
    // Update residual: r = r - stepSize * Ap
    vectorAdd(residual, residual, matrixTimesSearchDirection, -stepSize);

    // Check for convergence
    currentResidualNorm = std::sqrt(dotProduct(residual, residual));

    // Sanity check on residual values
    assert(!std::isnan(currentResidualNorm) && "Residual norm is NaN");

    if (currentResidualNorm < tolerance)
      return {solution, currentResidualNorm, iteration + 1, true};

    // Apply Preconditioner: z = M^-1 * r
    for (int i = 0; i < numUnknowns; i++)
      preconditionedResidual[i] = residual[i] * preconditionerInverse[i];

    double newResidualDotPreconditioned =
        dotProduct(residual, preconditionedResidual);

    if (std::abs(residualDotPreconditioned) < divisionByZeroThreshold) {
      return {solution, currentResidualNorm, iteration, false};
    }

    double directionUpdateFactor =
        newResidualDotPreconditioned / residualDotPreconditioned;

    // Update search direction: p = z + beta * p
    vectorAdd(searchDirection, preconditionedResidual, searchDirection,
              directionUpdateFactor);

    residualDotPreconditioned = newResidualDotPreconditioned;
  }

  return {solution, currentResidualNorm, iteration, false};
}

// ============================================================================
// DIRECT DENSE SOLVER (LU DECOMPOSITION WITH PARTIAL PIVOTING)
// ============================================================================

/**
 * Converts a sparse CSR matrix to a dense row-major vector representation.
 * Optimized for small matrices where direct solvers are preferred.
 */
inline std::vector<double> convertCsrToDense(const Csr_matrix &matrix) {
    std::vector<double> dense(matrix.rows * matrix.cols, 0.0);
    for (int i = 0; i < matrix.rows; ++i) {
        for (int j = matrix.row_pointer[i]; j < matrix.row_pointer[i+1]; ++j) {
            dense[i * matrix.cols + matrix.col_indices[j]] = matrix.values[j];
        }
    }
    return dense;
}

/**
 * solveLU_Pivoted - Robust Direct Solver for Non-Symmetric Matrices.
 * 
 * Performs Crout/Doolittle LU decomposition with Partial Pivoting (row swapping).
 * This is the fallback solver for circuits where iterative methods (PCG) fail
 * due to zero-diagonals or non-symmetry (active components like BJTs).
 */
inline SolverResult solveLU_Pivoted(const Csr_matrix &matrix, 
                                   const std::vector<double> &rhsVector) {
    const int n = matrix.rows;
    assert(matrix.rows == matrix.cols && "LU requires a square matrix");
    assert(matrix.rows == (int)rhsVector.size() && "Dimension mismatch");

    std::vector<double> A = convertCsrToDense(matrix);
    std::vector<double> b = rhsVector;
    std::vector<double> x(n, 0.0);
    std::vector<int> pivot(n);
    for (int i = 0; i < n; ++i) pivot[i] = i;

    // --- 1. LU Decomposition with Partial Pivoting ---
    for (int i = 0; i < n; ++i) {
        // Find pivot for column i
        double maxVal = 0.0;
        int maxRow = i;
        for (int k = i; k < n; ++k) {
            double val = std::abs(A[k * n + i]);
            if (val > maxVal) {
                maxVal = val;
                maxRow = k;
            }
        }

        // Check for singularity
        if (maxVal < 1e-25) {
            // Matrix is singular or near-singular. Return failure.
            std::cerr << "LU: Singular matrix at row " << i << " (maxVal=" << maxVal << ")\n";
            std::cerr << "Diagonals: ";
            for(int d=0; d<n; ++d) std::cerr << A[d*n+d] << " ";
            std::cerr << "\n";
            return {x, 1.0, 0, false};
        }

        // Swap rows if necessary
        if (maxRow != i) {
            std::swap(pivot[i], pivot[maxRow]);
            for (int k = 0; k < n; ++k) {
                std::swap(A[i * n + k], A[maxRow * n + k]);
            }
            std::swap(b[i], b[maxRow]);
        }

        // Compute multipliers and eliminate
        for (int j = i + 1; j < n; ++j) {
            A[j * n + i] /= A[i * n + i];
            for (int k = i + 1; k < n; ++k) {
                A[j * n + k] -= A[j * n + i] * A[i * n + k];
            }
        }
    }

    // --- 2. Forward Substitution (Ly = b) ---
    std::vector<double> y(n, 0.0);
    for (int i = 0; i < n; ++i) {
        y[i] = b[i];
        for (int j = 0; j < i; ++j) {
            y[i] -= A[i * n + j] * y[j];
        }
    }

    // --- 3. Backward Substitution (Ux = y) ---
    for (int i = n - 1; i >= 0; --i) {
        x[i] = y[i];
        for (int j = i + 1; j < n; ++j) {
            x[i] -= A[i * n + j] * x[j];
        }
        x[i] /= A[i * n + i];
    }

    // Verify residual (||Ax-b||)
    std::vector<double> check(n, 0.0);
    matrixMultiplication(matrix, x.data(), check.data());
    double residual = 0.0;
    for (int i = 0; i < n; ++i) {
        double err = check[i] - rhsVector[i];
        residual += err * err;
    }
    residual = std::sqrt(residual);

    return {x, residual, 1, true};
}
