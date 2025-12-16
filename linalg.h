#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

// Research-grade Dense Matrix (Flat 1D memory layout)
// Chapter 1: The Foundation - Efficient Dense Matrices
class Matrix {
private:
    std::vector<double> data;
    int rows_, cols_;

public:
    // 1.1: Flat Array Storage
    Matrix(int r = 0, int c = 0) : rows_(r), cols_(c) {
        data.resize(rows_ * cols_, 0.0);
    }

    void resize(int r, int c) {
        rows_ = r; cols_ = c;
        data.assign(rows_ * cols_, 0.0);
    }

    // 1.2: Access Operators (Row-Major mapping: index = row * cols + col)
    inline double& operator()(int i, int j) { 
        return data[i * cols_ + j]; 
    }
    
    inline double operator()(int i, int j) const { 
        return data[i * cols_ + j]; 
    }

    // Access raw data for low-level optimizations (e.g. BLAS/LAPACK calls later)
    double* raw_data() { return data.data(); }
    const double* raw_data() const { return data.data(); }

    int getRows() const { return rows_; }
    int getCols() const { return cols_; }

    void clear() {
        std::fill(data.begin(), data.end(), 0.0);
    }

    void print() const {
        for (int i = 0; i < rows_; ++i) {
            for (int j = 0; j < cols_; ++j)
                printf("%12.6g ", (*this)(i, j));
            printf("\n");
        }
    }
};

// LU decomposition result
struct LUResult {
    Matrix L, U;
    std::vector<int> P;
};

// Doolittle LU with partial pivoting (dense)
inline LUResult luDecompose(const Matrix& A) {
    int n = A.getRows();
    Matrix L(n, n), U(n, n);
    std::vector<int> P(n);

    for (int i = 0; i < n; ++i) {
        P[i] = i;
        L(i,i) = 1.0;
        for (int j = 0; j < n; ++j) U(i,j) = A(i,j);
    }

    for (int k = 0; k < n; ++k) {
        double maxv = std::abs(U(k,k));
        int pivot = k;
        for (int i = k+1; i < n; ++i) {
            if (std::abs(U(i,k)) > maxv) { maxv = std::abs(U(i,k)); pivot = i; }
        }
        if (pivot != k) {
            for (int j = 0; j < n; ++j) std::swap(U(k,j), U(pivot,j));
            std::swap(P[k], P[pivot]);
        }
        for (int i = k+1; i < n; ++i) {
            L(i,k) = U(i,k) / U(k,k);
            for (int j = k; j < n; ++j) U(i,j) -= L(i,k) * U(k,j);
        }
    }

    return {L, U, P};
}

inline std::vector<double> forwardSubstitution(const Matrix& L, const std::vector<double>& b, const std::vector<int>& P) {
    int n = L.getRows();
    std::vector<double> y(n);
    std::vector<double> pb(n);
    for (int i = 0; i < n; ++i) pb[i] = b[P[i]];
    for (int i = 0; i < n; ++i) {
        double s = pb[i];
        for (int j = 0; j < i; ++j) s -= L(i,j) * y[j];
        y[i] = s / L(i,i);
    }
    return y;
}

inline std::vector<double> backwardSubstitution(const Matrix& U, const std::vector<double>& y) {
    int n = U.getRows();
    std::vector<double> x(n);
    for (int ii = n-1; ii >= 0; --ii) {
        double s = y[ii];
        for (int j = ii+1; j < n; ++j) s -= U(ii,j) * x[j];
        x[ii] = s / U(ii,ii);
    }
    return x;
}

inline std::vector<double> solveLinearSystem(const Matrix& A, const std::vector<double>& b) {
    LUResult lu = luDecompose(A);
    std::vector<double> y = forwardSubstitution(lu.L, b, lu.P);
    std::vector<double> x = backwardSubstitution(lu.U, y);
    return x;
}
// Chapter 2: The Reality - Sparse Matrices
struct Triplet {
    int row, col;
    double value;
};

// Sparse Matrix in Coordinate List (COO) format
// Optimized for construction (stamping)
class SparseMatrix {
private:
    std::vector<Triplet> triplets;
    int rows_, cols_;

public:
    SparseMatrix(int r = 0, int c = 0) : rows_(r), cols_(c) {}

    void resize(int r, int c) {
        rows_ = r; cols_ = c;
        triplets.clear();
    }

    // Fast O(1) insertion for stamping components
    void add(int r, int c, double val) {
        if (std::abs(val) > 1e-12) { // simple thresholding
            triplets.push_back({r, c, val});
        }
    }

    // Clear for next time step
    void clear() {
        triplets.clear();
    }

    int getRows() const { return rows_; }
    int getCols() const { return cols_; }
    const std::vector<Triplet>& getTriplets() const { return triplets; }

    // Convert to dense for verification with existing solvers
    Matrix toDense() const {
        Matrix dense(rows_, cols_);
        for (const auto& t : triplets) {
            dense(t.row, t.col) += t.value; // += handles duplicates by summing
        }
        return dense;
    }

    void print() const {
        printf("SparseMatrix (%dx%d), %zu non-zeros:\n", rows_, cols_, triplets.size());
        for (const auto& t : triplets) {
            printf("  (%d, %d) -> %.6g\n", t.row, t.col, t.value);
        }
    }

    // Chapter 2.2: Compressed Sparse Row (CSR)
    struct CSR {
        std::vector<double> values;
        std::vector<int> col_indices;
        std::vector<int> row_ptr;
        int rows, cols;
    };

    CSR toCSR() const {
        CSR csr;
        csr.rows = rows_;
        csr.cols = cols_;
        csr.row_ptr.assign(rows_ + 1, 0);

        // 1. Count entries per row
        for (const auto& t : triplets) {
            if (t.row >= 0 && t.row < rows_)
                csr.row_ptr[t.row + 1]++;
        }

        // 2. Cumulative sum to get row pointers
        for (int i = 0; i < rows_; ++i) {
            csr.row_ptr[i + 1] += csr.row_ptr[i];
        }

        // 3. Fill values and column indices
        csr.values.resize(triplets.size());
        csr.col_indices.resize(triplets.size());
        std::vector<int> current_pos = csr.row_ptr; // copy to track insertion

        for (const auto& t : triplets) {
            if (t.row >= 0 && t.row < rows_) {
                int p = current_pos[t.row]++;
                csr.values[p] = t.value;
                csr.col_indices[p] = t.col;
            }
        }
        
        // Note: For a real solver, we might want to sort columns within each row here
        return csr;
    }
};
