#pragma once
#include "linalg.h"
#include <vector>
#include <stdexcept>

using std::invalid_argument;
using std::max;

/**
 * @brief CircuitSim performs Modified Nodal Analysis (MNA)
 *        for linear DC circuits consisting of:
 *        - Resistors
 *        - Independent voltage sources
 *
 *        Nodes are numbered starting at 1.
 *        Node 0 is ground.
 */
class CircuitSim
{
public:
    CircuitSim();
    ~CircuitSim();

    void addResistor(int node1, int node2, double resistance);
    void addVoltageSource(int node1, int node2, double voltage);

    // Solve returns a std::vector<double> with node voltages then branch currents
    std::vector<double> solve();

private:
    // Number of circuit nodes (dynamic: grows as needed)
    int numNodes = 0;

    // Number of voltage sources
    int numVoltageSources = 0;

    // MNA matrices (will be resized dynamically)
    Matrix G;   // Conductance matrix (NxN)
    Matrix B;   // Voltage source incidence (NxM)
    Matrix C;   // Transpose of B (MxN)
    Matrix D;   // Zero matrix (MxM)
    std::vector<double> z;   // RHS vector (N + M)

    // Storage of device definitions (optional)
    struct Resistor { int n1, n2; double R; };
    struct VoltageSource { int n1, n2; double V; };

    std::vector<Resistor> resistors;
    std::vector<VoltageSource> voltageSources;

    // Internal helper to resize the MNA matrices
    void resizeSystem();
};