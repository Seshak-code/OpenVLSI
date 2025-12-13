/*
 * MATHEMATICAL FUNCTIONS FOR MODIFIED NODAL ANALYSIS (MNA) CIRCUIT SIMULATOR
 * ==========================================================================
 * 
 * Modified Nodal Analysis converts a circuit into a system of linear equations:
 * [G  B] [v]   [i]
 * [C  D] [j] = [e]
 * 
 * Where:
 * - G: Conductance matrix (nxn) - describes resistive connections
 * - B: Input matrix (nxm) - describes voltage sources
 * - C: Output matrix (mxn) - describes voltage source connections
 * - D: Usually zero matrix (mxm)
 * - v: Node voltages (unknown)
 * - j: Branch currents through voltage sources (unknown)
 * - i: Known current sources
 * - e: Known voltage sources
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>

// ============================================================================
// 1. MATRIX OPERATIONS
// ============================================================================

class Matrix {
private:
    std::vector<std::vector<double>> data;
    int rows, cols;

public:
    Matrix(int r, int c) : rows(r), cols(c) {
        data.resize(rows, std::vector<double>(cols, 0.0));
    }

    double& operator()(int i, int j) { return data[i][j]; }
    double operator()(int i, int j) const { return data[i][j]; }
    
    int getRows() const { return rows; }
    int getCols() const { return cols; }

    // Matrix addition
    Matrix operator+(const Matrix& other) const {
        Matrix result(rows, cols);
        for(int i = 0; i < rows; i++)
            for(int j = 0; j < cols; j++)
                result(i,j) = data[i][j] + other(i,j);
        return result;
    }

    // Matrix multiplication
    Matrix operator*(const Matrix& other) const {
        Matrix result(rows, other.cols);
        for(int i = 0; i < rows; i++)
            for(int j = 0; j < other.cols; j++)
                for(int k = 0; k < cols; k++)
                    result(i,j) += data[i][k] * other(k,j);
        return result;
    }

    void print() const {
        for(int i = 0; i < rows; i++) {
            for(int j = 0; j < cols; j++)
                std::cout << std::setw(12) << std::setprecision(6) << data[i][j] << " ";
            std::cout << "\n";
        }
    }
};

// ============================================================================
// 2. LU DECOMPOSITION
// ============================================================================
// Purpose: Decompose matrix A into L (lower) and U (upper) triangular matrices
// Used for efficient solving of linear systems Ax = b
// Algorithm: Doolittle's method with partial pivoting

struct LUResult {
    Matrix L, U;
    std::vector<int> P; // Permutation vector for partial pivoting
};

LUResult luDecompose(const Matrix& A) {
    int n = A.getRows();
    Matrix L(n, n), U(n, n);
    std::vector<int> P(n);
    
    // Initialize L as identity and U as A
    for(int i = 0; i < n; i++) {
        P[i] = i;
        L(i,i) = 1.0;
        for(int j = 0; j < n; j++)
            U(i,j) = A(i,j);
    }

    // Doolittle's algorithm with partial pivoting
    for(int k = 0; k < n; k++) {
        // Find pivot
        double max = std::abs(U(k,k));
        int pivot = k;
        for(int i = k+1; i < n; i++) {
            if(std::abs(U(i,k)) > max) {
                max = std::abs(U(i,k));
                pivot = i;
            }
        }

        // Swap rows in U and P
        if(pivot != k) {
            for(int j = 0; j < n; j++)
                std::swap(U(k,j), U(pivot,j));
            std::swap(P[k], P[pivot]);
        }

        // Eliminate column k
        for(int i = k+1; i < n; i++) {
            L(i,k) = U(i,k) / U(k,k);
            for(int j = k; j < n; j++)
                U(i,j) -= L(i,k) * U(k,j);
        }
    }

    return {L, U, P};
}

// ============================================================================
// 3. FORWARD SUBSTITUTION
// ============================================================================
// Purpose: Solve Ly = b where L is lower triangular
// Used as first step in solving Ax = b after LU decomposition

std::vector<double> forwardSubstitution(const Matrix& L, const std::vector<double>& b, 
                                        const std::vector<int>& P) {
    int n = L.getRows();
    std::vector<double> y(n);
    
    // Apply permutation to b
    std::vector<double> pb(n);
    for(int i = 0; i < n; i++)
        pb[i] = b[P[i]];

    // Forward substitution: solve Ly = Pb
    for(int i = 0; i < n; i++) {
        y[i] = pb[i];
        for(int j = 0; j < i; j++)
            y[i] -= L(i,j) * y[j];
        y[i] /= L(i,i);
    }

    return y;
}

// ============================================================================
// 4. BACKWARD SUBSTITUTION
// ============================================================================
// Purpose: Solve Ux = y where U is upper triangular
// Used as second step in solving Ax = b after LU decomposition

std::vector<double> backwardSubstitution(const Matrix& U, const std::vector<double>& y) {
    int n = U.getRows();
    std::vector<double> x(n);

    // Backward substitution: solve Ux = y
    for(int i = n-1; i >= 0; i--) {
        x[i] = y[i];
        for(int j = i+1; j < n; j++)
            x[i] -= U(i,j) * x[j];
        x[i] /= U(i,i);
    }

    return x;
}

// ============================================================================
// 5. LINEAR SYSTEM SOLVER
// ============================================================================
// Purpose: Solve Ax = b using LU decomposition
// This is the main function used in MNA to solve circuit equations

std::vector<double> solveLinearSystem(const Matrix& A, const std::vector<double>& b) {
    // Step 1: LU decomposition with partial pivoting
    LUResult lu = luDecompose(A);
    
    // Step 2: Forward substitution (Ly = Pb)
    std::vector<double> y = forwardSubstitution(lu.L, b, lu.P);
    
    // Step 3: Backward substitution (Ux = y)
    std::vector<double> x = backwardSubstitution(lu.U, y);
    
    return x;
}

// ============================================================================
// 6. CIRCUIT ELEMENT STAMP FUNCTIONS
// ============================================================================
// These functions "stamp" circuit elements into the MNA matrices

// RESISTOR: Adds conductance (1/R) to G matrix
void stampResistor(Matrix& G, int node1, int node2, double R) {
    double g = 1.0 / R; // Conductance
    if(node1 >= 0) {
        G(node1, node1) += g;
        if(node2 >= 0) G(node1, node2) -= g;
    }
    if(node2 >= 0) {
        G(node2, node2) += g;
        if(node1 >= 0) G(node2, node1) -= g;
    }
}

// CURRENT SOURCE: Adds current to i vector
void stampCurrentSource(std::vector<double>& i, int node1, int node2, double I) {
    if(node1 >= 0) i[node1] -= I;
    if(node2 >= 0) i[node2] += I;
}

// VOLTAGE SOURCE: Adds to B, C matrices and e vector
void stampVoltageSource(Matrix& B, Matrix& C, std::vector<double>& e, 
                       int node1, int node2, int vsIndex, double V) {
    if(node1 >= 0) {
        B(node1, vsIndex) = 1.0;
        C(vsIndex, node1) = 1.0;
    }
    if(node2 >= 0) {
        B(node2, vsIndex) = -1.0;
        C(vsIndex, node2) = -1.0;
    }
    e[vsIndex] = V;
}

// CAPACITOR: Adds capacitance effect (for AC or transient analysis)
// C * dV/dt = I, discretized as: C * (V[n+1] - V[n]) / dt = I
void stampCapacitor(Matrix& G, int node1, int node2, double C, double dt) {
    double g_eq = C / dt; // Equivalent conductance for time discretization
    if(node1 >= 0) {
        G(node1, node1) += g_eq;
        if(node2 >= 0) G(node1, node2) -= g_eq;
    }
    if(node2 >= 0) {
        G(node2, node2) += g_eq;
        if(node1 >= 0) G(node2, node1) -= g_eq;
    }
}

// INDUCTOR: Similar to voltage source with additional equation
// V = L * dI/dt, discretized as: V = L * (I[n+1] - I[n]) / dt
void stampInductor(Matrix& B, Matrix& C, Matrix& D, int node1, int node2, 
                   int vsIndex, double L, double dt) {
    if(node1 >= 0) {
        B(node1, vsIndex) = 1.0;
        C(vsIndex, node1) = 1.0;
    }
    if(node2 >= 0) {
        B(node2, vsIndex) = -1.0;
        C(vsIndex, node2) = -1.0;
    }
    D(vsIndex, vsIndex) = -L / dt; // Equivalent resistance
}

// ============================================================================
// 7. EXAMPLE CIRCUIT WITH ALL ELEMENT TYPES
// ============================================================================

void simulateComplexCircuit() {
    std::cout << "\n=== CIRCUIT SIMULATOR WITH ALL ELEMENT TYPES ===\n\n";
    
    std::cout << "Circuit Description:\n";
    std::cout << "-------------------\n";
    std::cout << "Node 0: Ground (reference)\n";
    std::cout << "Node 1: Connected to V1(+), R1, C1\n";
    std::cout << "Node 2: Connected to R1, R2, L1, I1\n";
    std::cout << "Node 3: Connected to R2, V2(-), C1\n\n";
    
    std::cout << "Elements:\n";
    std::cout << "V1: 10V voltage source (Node 1 to Ground)\n";
    std::cout << "R1: 1kΩ resistor (Node 1 to Node 2)\n";
    std::cout << "R2: 2kΩ resistor (Node 2 to Node 3)\n";
    std::cout << "I1: 5mA current source (Ground to Node 2)\n";
    std::cout << "C1: 1µF capacitor (Node 1 to Node 3)\n";
    std::cout << "L1: 10mH inductor (Node 2 to Ground)\n";
    std::cout << "V2: 5V voltage source (Ground to Node 3)\n\n";

    // Circuit parameters
    int numNodes = 3; // Excluding ground
    int numVS = 2;    // Number of voltage sources
    double dt = 1e-6; // Time step for transient analysis (1 µs)

    // Component values
    double V1 = 10.0;           // 10V
    double V2 = 5.0;            // 5V
    double R1 = 1000.0;         // 1kΩ
    double R2 = 2000.0;         // 2kΩ
    double I1 = 0.005;          // 5mA
    double C1 = 1e-6;           // 1µF
    double L1 = 10e-3;          // 10mH

    // Initialize MNA matrices
    int totalSize = numNodes + numVS;
    Matrix A(totalSize, totalSize);
    std::vector<double> b(totalSize, 0.0);

    // Create sub-matrices
    Matrix G(numNodes, numNodes);
    Matrix B(numNodes, numVS);
    Matrix C(numVS, numNodes);
    Matrix D(numVS, numVS);
    std::vector<double> i(numNodes, 0.0);
    std::vector<double> e(numVS, 0.0);

    // Stamp all elements
    stampResistor(G, 0, 1, R1);      // R1: Node 1 to Node 2
    stampResistor(G, 1, 2, R2);      // R2: Node 2 to Node 3
    stampCurrentSource(i, -1, 1, I1); // I1: Ground to Node 2
    stampCapacitor(G, 0, 2, C1, dt); // C1: Node 1 to Node 3
    
    stampVoltageSource(B, C, e, 0, -1, 0, V1); // V1: Node 1 to Ground
    stampVoltageSource(B, C, e, 2, -1, 1, V2); // V2: Node 3 to Ground
    stampInductor(B, C, D, 1, -1, 0, L1, dt);  // L1 effect on V1

    // Assemble complete MNA matrix
    for(int i = 0; i < numNodes; i++) {
        for(int j = 0; j < numNodes; j++)
            A(i, j) = G(i, j);
        for(int j = 0; j < numVS; j++)
            A(i, numNodes + j) = B(i, j);
        b[i] = i[i];
    }
    for(int i = 0; i < numVS; i++) {
        for(int j = 0; j < numNodes; j++)
            A(numNodes + i, j) = C(i, j);
        for(int j = 0; j < numVS; j++)
            A(numNodes + i, numNodes + j) = D(i, j);
        b[numNodes + i] = e[i];
    }

    // Solve the system
    std::cout << "MNA Matrix A:\n";
    A.print();
    std::cout << "\nRight-hand side vector b:\n";
    for(double val : b)
        std::cout << std::setw(12) << val << "\n";

    std::vector<double> x = solveLinearSystem(A, b);

    // Display results
    std::cout << "\n=== SIMULATION RESULTS ===\n";
    std::cout << "\nNode Voltages:\n";
    for(int i = 0; i < numNodes; i++)
        std::cout << "  V" << (i+1) << " = " << std::setw(10) << std::setprecision(6) 
                  << x[i] << " V\n";
    
    std::cout << "\nVoltage Source Currents:\n";
    for(int i = 0; i < numVS; i++)
        std::cout << "  I_V" << (i+1) << " = " << std::setw(10) << std::setprecision(6) 
                  << x[numNodes + i] * 1000 << " mA\n";

    std::cout << "\nElement Power Dissipation:\n";
    std::cout << "  P_R1 = " << std::pow(x[0] - x[1], 2) / R1 * 1000 << " mW\n";
    std::cout << "  P_R2 = " << std::pow(x[1] - x[2], 2) / R2 * 1000 << " mW\n";
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main() {
    std::cout << std::fixed << std::setprecision(6);
    
    std::cout << "C++ CIRCUIT SIMULATOR USING MODIFIED NODAL ANALYSIS\n";
    std::cout << "====================================================\n";
    
    simulateComplexCircuit();
    
    return 0;
}

/*
 * MATHEMATICAL FUNCTIONS SUMMARY:
 * ================================
 * 
 * 1. Matrix Operations (addition, multiplication)
 *    - Used to construct and manipulate MNA matrices
 * 
 * 2. LU Decomposition (Doolittle's method with partial pivoting)
 *    - Decomposes A into L and U for efficient solving
 *    - Complexity: O(n³)
 * 
 * 3. Forward Substitution
 *    - Solves Ly = Pb where L is lower triangular
 *    - Complexity: O(n²)
 * 
 * 4. Backward Substitution
 *    - Solves Ux = y where U is upper triangular
 *    - Complexity: O(n²)
 * 
 * 5. Linear System Solver
 *    - Combines LU decomposition and substitutions
 *    - Overall complexity: O(n³)
 * 
 * 6. Circuit Element Stamping
 *    - Resistor: Ohm's law (V = IR, or I = V/R = gV)
 *    - Current source: Direct current injection
 *    - Voltage source: Constraint equation (V = constant)
 *    - Capacitor: I = C(dV/dt), discretized using backward Euler
 *    - Inductor: V = L(dI/dt), discretized using backward Euler
 * 
 * KEY EQUATIONS:
 * - Kirchhoff's Current Law (KCL): Σ I_in = Σ I_out at each node
 * - Kirchhoff's Voltage Law (KVL): Σ V = 0 around each loop
 * - Ohm's Law: V = IR
 * - Capacitor: Q = CV, I = dQ/dt = C(dV/dt)
 * - Inductor: Φ = LI, V = dΦ/dt = L(dI/dt)
 */