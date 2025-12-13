#pragma once
#include <Eigen/Dense>
#include <vector>

std namespace;

/**
 * @brief CircuitSim performs Modified Nodal Analysis (MNA)
 *        for linear DC circuits consisting of:
 *        - Resistors
 *        - Independent voltage sources
 *
 *        Nodes are numbered starting at 1.
 *        Node 0 is ground.
 *
 *        Mathematical basis (MNA):
 *
 *        For N nodes and M voltage sources:
 *        Unknown vector x = [ V1 V2 ... VN  I_V1 ... I_VM ]^T
 *
 *        System of equations:
 *            A * x = z
 *
 *        Where:
 *        - G  = conductance matrix (resistors)
 *        - B  = incidence matrix for voltage sources
 *        - C  = Bᵀ
 *        - D  = 0 matrix because no dependent sources
 *
 *        Full matrix:
 * 
 *            |  G   B |   | V |   | Iinj |
 *            |  C   0 | * | I | = | Vs   |
 *
 *        After stamping all devices, call solve() to
 *        compute all node voltages and source currents.
 */
class CircuitSim
{
public:
    CircuitSim();
    ~CircuitSim();

    /**
     * @brief Add a resistor between two nodes.
     *
     * Electrical model:
     *      I = (Vn1 - Vn2) / R
     * Conductance:
     *      G = 1 / R
     *
     * MNA stamp on G matrix:
     *
     *      G[n1][n1] += G
     *      G[n2][n2] += G
     *      G[n1][n2] -= G
     *      G[n2][n1] -= G
     *
     * @param node1  First node (1..N)
     * @param node2  Second node (1..N)
     * @param resistance  Resistance in ohms
     */
    void addResistor(int node1, int node2, double resistance)
    {
        // Ensure nodes are valid
        if (node1 < 0 || node2 < 0 || resistance <= 0.0) {
            throw invalid_argument("Invalid node number or resistance value.");
        }

        // Update number of nodes
        int maxNode = max(node1, node2);
        if (maxNode >= numNodes) {
            numNodes = maxNode + 1; // +1 for ground node 0
            resizeSystem();
        }

        // Calculate conductance
        double G_value = 1.0 / resistance;

        // Stamp G matrix
        if (node1 != 0) {
            G(node1 - 1, node1 - 1) += G_value;
        }
        if (node2 != 0) {
            G(node2 - 1, node2 - 1) += G_value;
        }
        if (node1 != 0 && node2 != 0) {
            G(node1 - 1, node2 - 1) -= G_value;
            G(node2 - 1, node1 - 1) -= G_value;
        }

        // Store resistor definition (optional)
        resistors.push_back({node1, node2, resistance});
    };

    /**
     * @brief Add an independent voltage source.
     *
     * Electrical model:
     *      V(node1) - V(node2) = Vs
     *
     * MNA stamp:
     *      New current variable Iv is created.
     *
     *      B[n1][v] = +1
     *      B[n2][v] = -1
     *
     *      C[v][n1] = +1
     *      C[v][n2] = -1
     *
     *      D[v][v] = 0
     *
     * RHS stamp:
     *      z_Iv = Vs
     */
    void addVoltageSource(int node1, int node2, double voltage)
    {
        
        if (node1 < 0 || node2 < 0) 
        {
            throw invalid_argument("Invalid node number.");
        }

        // Adds node and 
        int maxNode = max(node1, node2);
        if (maxNode >= numNodes) 
        {
            numNodes = maxNode + 1; // +1 for ground node 0
            resizeSystem();
        }

        // Increment voltage source count
        numVoltageSources++;
        resizeSystem();

        int vIndex = numVoltageSources - 1; // Index of new voltage source

        // Stamp B and C matrices
        if (node1 != 0) {
            B(node1 - 1, vIndex) += 1.0;
            C(vIndex, node1 - 1) += 1.0;
        }
        if (node2 != 0) {
            B(node2 - 1, vIndex) -= 1.0;
            C(vIndex, node2 - 1) -= 1.0;
        }

        // Stamp RHS vector z
        z(numNodes + vIndex) += voltage;

        // Store voltage source definition (optional)
        voltageSources.push_back({node1, node2, voltage});
    };

    /**
     * @brief Build the full MNA matrix A and RHS vector z,
     *        solve A * x = z for:
     *           - Node voltages V1..VN
     *           - Current through each voltage source
     *
     * @return Eigen::VectorXd The solution vector x
     */
    Eigen::VectorXd solve();

private:
    // Number of circuit nodes (dynamic: grows as needed)
    int numNodes;

    // Number of voltage sources
    int numVoltageSources;

    // MNA matrices (will be resized dynamically)
    Eigen::MatrixXd G;   // Conductance matrix (NxN)
    Eigen::MatrixXd B;   // Voltage source incidence (NxM)
    Eigen::MatrixXd C;   // Transpose of B (MxN)
    Eigen::MatrixXd D;   // Zero matrix (MxM)
    Eigen::VectorXd z;   // RHS vector (Nx + Mx)

    // Storage of device definitions (optional)
    struct Resistor {
        int n1, n2;
        double R;
    };
    struct VoltageSource {
        int n1, n2;
        double V;
    };

    std::vector<Resistor> resistors;
    std::vector<VoltageSource> voltageSources;

    // Internal helper to resize the MNA matrices
    void resizeSystem();
};