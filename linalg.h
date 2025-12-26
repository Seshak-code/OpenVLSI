#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <omp.h>

/**
 * @file linalg.h
 * @brief High-performance sparse linear algebra library for circuit simulation.
 * 
 * This library provides a complete toolchain for solving sparse linear systems (Ax = b):
 * 1. CSR (Compressed Sparse Row) matrix storage format.
 * 2. Triplet-based matrix assembly with duplicate handling.
 * 3. OpenMP-parallelized Sparse Matrix-Vector Multiplication (SpMV).
 * 4. Preconditioned Conjugate Gradient (PCG) iterative solver.
 */

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * @brief Compressed Sparse Row (CSR) matrix representation.
 * 
 * This format stores only non-zero elements for memory efficiency.
 * - values: Non-zero values array.
 * - col_indices: Column index for each value.
 * - row_pointer: Starting index of each row in `values` (size = rows + 1).
 */
struct Csr_matrix {
    std::vector<double> values;
    std::vector<int> col_indices;
    std::vector<int> row_pointer;
    int rows;
    int cols;
    int nnz; // Number of non-zeros
};

/**
 * @brief A single matrix entry before compression.
 */
struct Triplet {
    int row;
    int col;
    double value;
};

// ============================================================================
// MATRIX ASSEMBLY
// ============================================================================

/**
 * @brief Builds a sparse matrix from individual triplet entries.
 * 
 * Allows adding duplicate (row, col) pairs; values are summed during CSR conversion.
 * This is essential for circuit simulation where parallel components contribute
 * to the same matrix entry.
 */
class MatrixConstructor {
private:
    std::vector<Triplet> triplets;
    int numRows = 0;
    int numCols = 0;

public:
    /**
     * @brief Adds a value to the matrix at position (row, col).
     * @param row Row index (0-indexed).
     * @param col Column index (0-indexed).
     * @param value The value to add.
     */
    void add(int row, int col, double value)
    {
        triplets.push_back({row, col, value});
        if (row >= numRows) 
            numRows = row + 1;
        if (col >= numCols) 
            numCols = col + 1;
    }

    /**
     * @brief Converts the accumulated triplets into a CSR matrix.
     * 
     * This method sorts triplets by (row, col), merges duplicates by summing
     * their values, and constructs the final CSR arrays.
     * @return Csr_matrix The resulting sparse matrix.
     */
    Csr_matrix createCsr()
    {
        Csr_matrix matrix;
        matrix.rows = numRows;
        matrix.cols = numCols;

        if (triplets.empty()) {
            matrix.row_pointer.assign(numRows + 1, 0);
            return matrix;
        }

        // Sort triplets by row, then by column for duplicate merging
        std::sort(triplets.begin(), triplets.end(), [](const Triplet& a, const Triplet& b) 
        {
            return (a.row < b.row || (a.row == b.row && a.col < b.col));
        });

        matrix.row_pointer.assign(numRows + 1, 0);

        // Merge duplicates and fill values/columns
        for (int tripletIndex = 0; tripletIndex < (int)triplets.size(); tripletIndex++)
        {
            const Triplet& currentTriplet = triplets[tripletIndex];
            
            // Check if this triplet has the same (row, col) as the previous one
            if (tripletIndex > 0 && 
                currentTriplet.row == triplets[tripletIndex - 1].row && 
                currentTriplet.col == triplets[tripletIndex - 1].col)
            {
                // Sum values for duplicate entries
                if (!matrix.values.empty()) {
                    matrix.values.back() += currentTriplet.value;
                }
            }
            else
            {
                // New unique cell: store value and column, increment row count
                matrix.values.push_back(currentTriplet.value);
                matrix.col_indices.push_back(currentTriplet.col);
                matrix.row_pointer[currentTriplet.row + 1]++;
            }
        }

        // Convert histogram counts to starting indices (prefix sum)
        for (int rowIndex = 0; rowIndex < numRows; ++rowIndex)
            matrix.row_pointer[rowIndex + 1] += matrix.row_pointer[rowIndex];

        matrix.nnz = (int)matrix.values.size();
        return matrix;
    }
};

// ============================================================================
// SPARSE MATRIX-VECTOR MULTIPLICATION (SpMV)
// ============================================================================

/**
 * @brief Computes y = A * x using the CSR format.
 * 
 * This is the core operation of iterative solvers. It is parallelized with OpenMP.
 * @param matrix The sparse matrix A.
 * @param inputVector The input vector x.
 * @param outputVector The output vector y.
 */
inline void matrixMultiplication(const Csr_matrix& matrix, const double* inputVector, double* outputVector)
{
    #pragma omp parallel for
    for (int rowIndex = 0; rowIndex < matrix.rows; rowIndex++)
    {
        double rowSum = 0.0;
        int rowStart = matrix.row_pointer[rowIndex];
        int rowEnd   = matrix.row_pointer[rowIndex + 1];
        
        for (int elementIndex = rowStart; elementIndex < rowEnd; ++elementIndex) {
            rowSum += matrix.values[elementIndex] * inputVector[matrix.col_indices[elementIndex]];
        }
        outputVector[rowIndex] = rowSum;
    }
}

// ============================================================================
// VECTOR OPERATIONS (OpenMP Optimized)
// ============================================================================

/**
 * @brief Computes the dot product of two vectors.
 */
inline double dotProduct(const std::vector<double>& vectorA, const std::vector<double>& vectorB) {
    double sum = 0.0;
    #pragma omp parallel for reduction(+:sum)
    for (int index = 0; index < (int)vectorA.size(); index++) {
        sum += vectorA[index] * vectorB[index];
    }
    return sum;
}

/**
 * @brief Computes result = vectorA + scalar * vectorB.
 */
inline void vectorAdd(std::vector<double>& result, const std::vector<double>& vectorA, const std::vector<double>& vectorB, double scalar) {
    #pragma omp parallel for
    for (int index = 0; index < (int)vectorA.size(); index++) {
        result[index] = vectorA[index] + scalar * vectorB[index];
    }
}

/**
 * @brief Extracts the inverse of the diagonal elements of a CSR matrix.
 * 
 * This is used for the Jacobi preconditioner. If a diagonal element is zero
 * or very small, its inverse is set to 1.0 to avoid numerical issues.
 * @param matrix The input CSR matrix.
 * @return std::vector<double> A vector containing 1/A[i][i] for each row i.
 */
inline std::vector<double> getDiagonalInverse(const Csr_matrix& matrix) {
    const double zeroThreshold = 1e-18;
    std::vector<double> diagonalInverse(matrix.rows, 1.0);
    
    #pragma omp parallel for
    for (int rowIndex = 0; rowIndex < matrix.rows; rowIndex++) {
        for (int elementIndex = matrix.row_pointer[rowIndex]; elementIndex < matrix.row_pointer[rowIndex + 1]; elementIndex++) {
            if (matrix.col_indices[elementIndex] == rowIndex) {
                if (std::abs(matrix.values[elementIndex]) > zeroThreshold) {
                    diagonalInverse[rowIndex] = 1.0 / matrix.values[elementIndex];
                }
                break; // Diagonal found, no need to check further in this row
            }
        }
    }
    return diagonalInverse;
}

/**
 * @brief contains the results of a linear solver execution.
 */
struct SolverResult {
    std::vector<double> solution;
    double finalResidual;
    int iterations;
    bool converged;
};

/**
 * @brief Solves Ax = b using the Preconditioned Conjugate Gradient method.
 * 
 * Uses a Jacobi (diagonal) preconditioner for faster convergence.
 * 
 * @param matrix The sparse matrix A (must be symmetric positive definite).
 * @param rhsVector The right-hand side vector b.
 * @param tolerance Convergence tolerance (stops when ||r|| < tolerance).
 * @param maxIterations Maximum number of solver iterations.
 * @return SolverResult The solution vector, final residual, and iteration count.
 */
inline SolverResult solvePCG(
    const Csr_matrix& matrix, 
    const std::vector<double>& rhsVector, 
    double tolerance = 1e-10, 
    int maxIterations = 1000) 
{
    const double divisionByZeroThreshold = 1e-20;
    const int numUnknowns = matrix.rows;
    
    std::vector<double> solution(numUnknowns, 0.0);
    std::vector<double> residual = rhsVector; // residual = b - A*x, with x=0
    
    // Prepare Jacobi Preconditioner (M^-1 = 1/diag(A))
    std::vector<double> preconditionerInverse = getDiagonalInverse(matrix);
    
    // Apply preconditioner: preconditionedResidual = M^-1 * residual
    std::vector<double> preconditionedResidual(numUnknowns);
    for (int i = 0; i < numUnknowns; i++) {
        preconditionedResidual[i] = residual[i] * preconditionerInverse[i];
    }
    
    std::vector<double> searchDirection = preconditionedResidual;
    double residualDotPreconditioned = dotProduct(residual, preconditionedResidual);
    
    // Early exit if already converged
    double initialResidualNorm = std::sqrt(dotProduct(residual, residual));
    if (initialResidualNorm < tolerance) {
        return {solution, initialResidualNorm, 0, true};
    }

    int iteration = 0;
    double currentResidualNorm = initialResidualNorm;

    for (iteration = 0; iteration < maxIterations; iteration++) {
        // matrixTimesSearchDirection = A * searchDirection
        std::vector<double> matrixTimesSearchDirection(numUnknowns);
        matrixMultiplication(matrix, searchDirection.data(), matrixTimesSearchDirection.data());
        
        double searchDirectionDotAp = dotProduct(searchDirection, matrixTimesSearchDirection);
        if (std::abs(searchDirectionDotAp) < divisionByZeroThreshold) {
            break; // Avoid division by zero
        }

        double stepSize = residualDotPreconditioned / searchDirectionDotAp;
        
        // Update solution: x = x + stepSize * p
        vectorAdd(solution, solution, searchDirection, stepSize);
        // Update residual: r = r - stepSize * Ap
        vectorAdd(residual, residual, matrixTimesSearchDirection, -stepSize);
        
        // Check for convergence
        currentResidualNorm = std::sqrt(dotProduct(residual, residual));
        if (currentResidualNorm < tolerance) {
            return {solution, currentResidualNorm, iteration + 1, true};
        }

        // Apply Preconditioner: z = M^-1 * r
        for (int i = 0; i < numUnknowns; i++) {
            preconditionedResidual[i] = residual[i] * preconditionerInverse[i];
        }
        
        double newResidualDotPreconditioned = dotProduct(residual, preconditionedResidual);
        double directionUpdateFactor = newResidualDotPreconditioned / residualDotPreconditioned;
        
        // Update search direction: p = z + beta * p
        vectorAdd(searchDirection, preconditionedResidual, searchDirection, directionUpdateFactor);
        
        residualDotPreconditioned = newResidualDotPreconditioned;
    }
    
    return {solution, currentResidualNorm, iteration, false};
}
