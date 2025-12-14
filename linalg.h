#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

// Simple dense matrix class used by the simulator (research-grade replacement can
// be implemented later). Minimal API: construction, element access, rows/cols.
class Matrix {
private:
    std::vector<std::vector<double>> data;
    int rows_, cols_;

public:
    Matrix(int r = 0, int c = 0) : rows_(r), cols_(c) {
        data.assign(rows_, std::vector<double>(cols_, 0.0));
    }

    void resize(int r, int c) {
        rows_ = r; cols_ = c;
        data.assign(rows_, std::vector<double>(cols_, 0.0));
    }

    double& operator()(int i, int j) { return data[i][j]; }
    double operator()(int i, int j) const { return data[i][j]; }

    int getRows() const { return rows_; }
    int getCols() const { return cols_; }

    void clear() {
        for (int i = 0; i < rows_; ++i)
            std::fill(data[i].begin(), data[i].end(), 0.0);
    }

    void print() const {
        for (int i = 0; i < rows_; ++i) {
            for (int j = 0; j < cols_; ++j)
                printf("%12.6g ", data[i][j]);
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
