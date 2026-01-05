#include "physics/regression_suite.h"
#include <string>
#include <cstring>

int main(int argc, char* argv[]) {
    bool updateGolden = false;
    
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--update") == 0) {
            updateGolden = true;
        }
    }

    RegressionSuite suite;
    
    // Register Standard Benchmarks
    suite.registerTest("linear_divider_10v_2k_2k", Benchmarks::linearDivider);
    suite.registerTest("diode_iv_1a", Benchmarks::diodeCircuit);
    suite.registerTest("bridge_rectifier", Benchmarks::bridgeRectifier);

    // Run Suite
    suite.run(updateGolden);

    return 0;
}
