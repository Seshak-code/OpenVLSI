// Regression: detectCapabilities() must work as the FIRST GPU-touching call,
// exactly as AcuteSim's main() makes it. Its Dawn adapter probe used wgpu* C
// calls before anything installed dawn_proc's table and segfaulted at address 0.
#include "acutesim_engine/infrastructure/hardware_capabilities.h"
#include <cstdio>

int main() {
    const auto caps = acutesim::compute::detectCapabilities();
    std::printf("cores=%d simd=%s gpu=%d\n", caps.physicalCpuCores,
                acutesim::compute::simdTierName(caps.simdTier), caps.hasGPU ? 1 : 0);
    return caps.physicalCpuCores > 0 ? 0 : 1;
}
