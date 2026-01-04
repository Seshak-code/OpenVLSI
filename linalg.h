#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
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

class MatrixConstructor {
private:
  // Internal storage for raw coordinates
  std::vector<Triplet> triplets;
  // initializes the count of rows and columns
  int numRows = 0;
  int numCols = 0;

public:
  /*
   * Stamps a numerical value at the specified (row, col) coordinate.
   * row: 0-indexed row position.
   * col: 0-indexed column position.
   * value: Numerical value to add to this cell.
   */

  void add(int row, int col, double value) {
    // debugging assumed charateristics of parameters
    assert(row >= 0 && "Row index can't be negative");
    assert(col >= 0 && "Column index can't be negative");
    // value can be anything lol

    // Records new entry into the triplet array
    triplets.push_back({row, col, value});

    // updating the matrix boundaries
    if (row >= numRows)
      numRows = row + 1;
    if (col >= numCols)
      numCols = col + 1;

    // debugging to see if boundaries are updated correctly
    assert(numRows > row && "numRows logic error");
    assert(numCols > col && "numCols logic error");
  }

  /*
   * Converts uncompressed Triplets into the finalized CSR format.
   *
   * The conversion follows a standard sparse matrix construction algorithm:
   * 1. Sorting: Organizes Triplets by row and then column index.
   * 2. Deduplication: Sums values sharing identical (row, col) coordinates.
   * 3. CSR Mapping: Populates compressed values, columns, and row pointers.
   * 4. Prefix Sum: Transforms row counts into starting index offsets.
   */

  Csr_matrix createCsr() {
    // matrix object
    Csr_matrix matrix;

    matrix.rows = numRows;
    matrix.cols = numCols;

    // Makes sure an empty matrix is returned correctly
    if (triplets.empty()) {
      matrix.row_pointer.assign(numRows + 1, 0);
      return matrix;
    }

    // Sorting to group same entries for later
    // a lambda comparator used to sort triplets by (row, then column) for CSR
    // construction
    std::sort(triplets.begin(), triplets.end(),
              [](const Triplet &a, const Triplet &b) {
                // returns true when the sorting operation is complete
                return (a.row < b.row || (a.row == b.row && a.col < b.col));
              });

    // Initialize Row Pointer array (indices 0 to N)
    matrix.row_pointer.assign(numRows + 1, 0);

    // Verify internal state before processing
    assert(!triplets.empty() && "Triplets should not be empty here");
    assert(matrix.row_pointer.size() == (size_t)numRows + 1 &&
           "Row pointer size mismatch");

    // removes duplicates and initial CSR population
    const int numTriplets = (int)triplets.size(); // Use explicit loop bounds
    for (int tripletIndex = 0; tripletIndex < numTriplets; tripletIndex++) {
      const Triplet &currentTriplet = triplets[tripletIndex];

      // In-loop consistency check
      assert(currentTriplet.row < numRows && "Row index out of bounds");
      assert(currentTriplet.col < numCols && "Col index out of bounds");

      // Check for duplicate coordinates (entries assigned to the same matrix
      // cell)
      if (tripletIndex > 0 &&
          currentTriplet.row == triplets[tripletIndex - 1].row &&
          currentTriplet.col == triplets[tripletIndex - 1].col) {
        if (!matrix.values.empty()) {
          // Duplicate entries from various stamps are summed to enforce
          // superposition and correct KCL math
          matrix.values.back() += currentTriplet.value;
        }
      } else {
        // appends the unique coordinate to the compressed vectors
        matrix.values.push_back(currentTriplet.value);
        matrix.col_indices.push_back(currentTriplet.col);

        // Temporarily store the element count per row in the pointer array
        matrix.row_pointer[currentTriplet.row + 1]++;
      }
    }

    // Prefix Sum Transformation:
    // Converts the per-row element counts into starting index offsets.
    // row_pointer[i] will store the index where Row i begins in the 'values'
    // array.
    for (int rowIndex = 0; rowIndex < numRows; ++rowIndex)
      matrix.row_pointer[rowIndex + 1] += matrix.row_pointer[rowIndex];

    matrix.nnz = (int)matrix.values.size();

    // Post-condition validation
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
