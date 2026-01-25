#include "kernel_dispatch.h"
#include <iostream>

#ifdef _WIN32
#include <intrin.h>
#endif

// Forward declare the generic kernels (implemented in physics_tensors.cpp)
void batchDiodePhysics_generic(DiodeTensor& tensor, const std::vector<double>& voltages);
void batchMosfetPhysics_generic(MosfetTensor& tensor, const std::vector<double>& voltages);
void batchBJTPhysics_generic(BJTTensor& tensor, const std::vector<double>& voltages);

// Forward declare AVX kernels
#ifdef __AVX512F__
void batchDiodePhysics_avx512(DiodeTensor& tensor, const std::vector<double>& voltages);
#endif

namespace acutesim {
namespace compute {

KernelDispatcher& KernelDispatcher::get() {
    static KernelDispatcher instance;
    return instance;
}

KernelDispatcher::KernelDispatcher() {
    detectCpuAndBind();
}

void KernelDispatcher::detectCpuAndBind() {
    // Default to Generic
    architecture = SimdArch::Generic;
    batch_diode_physics = batchDiodePhysics_generic;
    batch_mosfet_physics = batchMosfetPhysics_generic;
    batch_bjt_physics = batchBJTPhysics_generic;

#if defined(__x86_64__) || defined(_M_X64)
    int regs[4];
    bool hasAVX2 = false;
    bool hasAVX512F = false;

    // Check CPUID leaf 7, subleaf 0
#ifdef _WIN32
    __cpuidex(regs, 7, 0);
#else
    __asm__ volatile ("cpuid"
        : "=a" (regs[0]), "=b" (regs[1]), "=c" (regs[2]), "=d" (regs[3])
        : "a" (7), "c" (0));
#endif

    hasAVX2 = (regs[1] & (1 << 5)) != 0;
    hasAVX512F = (regs[1] & (1 << 16)) != 0;

    if (hasAVX512F) {
        architecture = SimdArch::AVX512;
        std::cout << "[INFO] Compute: Detected AVX-512. Using optimized kernels." << std::endl;
        #ifdef __AVX512F__
        batch_diode_physics = batchDiodePhysics_avx512;
        #else
        std::cout << "[WARN] Compute: AVX-512 detected but binary compiled without AVX-512 support. Falling back to Generic." << std::endl;
        #endif
    } else if (hasAVX2) {
        architecture = SimdArch::AVX2;
        std::cout << "[INFO] Compute: Detected AVX2. (Mapping to Generic for now)" << std::endl;
        // Bind AVX2 kernels here when implemented
    }

#elif defined(__aarch64__)
    architecture = SimdArch::Neon;
    std::cout << "[INFO] Compute: Detected ARM Neon." << std::endl;
    // Bind Neon kernels here
#endif
    
    if (architecture == SimdArch::Generic) {
        std::cout << "[INFO] Compute: Using Generic C++ Kernels." << std::endl;
    }
}

} // namespace compute
} // namespace acutesim
