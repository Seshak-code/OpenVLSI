#include "hardware_capabilities.h"
#include <mutex>
#include <thread>

#ifdef _WIN32
#  include <intrin.h>
#  include <windows.h>
#elif defined(__APPLE__)
#  include <sys/sysctl.h>
#else
#  include <unistd.h>
#endif

// GPU adapter probing via Dawn (native builds only)
#if !defined(__EMSCRIPTEN__) && defined(ACUTESIM_HAS_DAWN_NATIVE)
#  include <webgpu/webgpu.h>
#  include <dawn/native/DawnNative.h>
#  include <dawn/dawn_proc.h>
#  define ACUTESIM_GPU_PROBE_AVAILABLE 1
#endif

namespace acutesim::compute {

// ============================================================================
// CPU core count
// ============================================================================

static int queryPhysicalCores() {
#if defined(__APPLE__)
    int cores = 1;
    size_t size = sizeof(cores);
    sysctlbyname("hw.physicalcpu", &cores, &size, nullptr, 0);
    return cores > 0 ? cores : 1;
#elif defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return static_cast<int>(info.dwNumberOfProcessors);
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? static_cast<int>(n) : 1;
#endif
}

// ============================================================================
// SIMD tier detection
// ============================================================================

static HardwareCapabilities::SIMDTier detectSIMDTier() {
#if defined(__x86_64__) || defined(_M_X64)
    int regs[4] = {};
#ifdef _WIN32
    __cpuidex(regs, 7, 0);
#else
    __asm__ volatile ("cpuid"
        : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
        : "a"(7), "c"(0));
#endif
    bool hasAVX512F = (regs[1] & (1 << 16)) != 0;
    bool hasAVX2    = (regs[1] & (1 <<  5)) != 0;

    if (hasAVX512F) return HardwareCapabilities::SIMDTier::AVX512;
    if (hasAVX2)    return HardwareCapabilities::SIMDTier::AVX2;
    return HardwareCapabilities::SIMDTier::SCALAR;

#elif defined(__aarch64__) || defined(_M_ARM64)
    return HardwareCapabilities::SIMDTier::NEON;

#else
    return HardwareCapabilities::SIMDTier::SCALAR;
#endif
}

// ============================================================================
// GPU probe
// ============================================================================

static void probeGPU(HardwareCapabilities& caps) {
#ifdef ACUTESIM_GPU_PROBE_AVAILABLE
    dawn::native::Instance dawnInst;
    dawnInst.DiscoverDefaultAdapters();
    auto adapters = dawnInst.GetAdapters();
    for (const auto& adapter : adapters) {
        WGPUAdapterProperties props{};
        adapter.GetProperties(&props);
        if (props.adapterType == WGPUAdapterType_DiscreteGPU ||
            props.adapterType == WGPUAdapterType_IntegratedGPU) {
            caps.hasGPU = true;
            caps.gpuAdapterName = props.name ? props.name : "Unknown GPU";
            break; // Take the first usable adapter
        }
    }
#else
    (void)caps; // GPU probing unavailable without Dawn
#endif
}

// ============================================================================
// Public API
// ============================================================================

HardwareCapabilities detectCapabilities() {
    static std::once_flag  flag;
    static HardwareCapabilities result;

    std::call_once(flag, [&]() {
        result.simdTier        = detectSIMDTier();
        result.physicalCpuCores = queryPhysicalCores();
        probeGPU(result);
    });

    return result;
}

const char* simdTierName(HardwareCapabilities::SIMDTier tier) {
    switch (tier) {
        case HardwareCapabilities::SIMDTier::AVX512: return "AVX-512";
        case HardwareCapabilities::SIMDTier::AVX2:   return "AVX2";
        case HardwareCapabilities::SIMDTier::NEON:   return "NEON";
        default:                                     return "Scalar";
    }
}

} // namespace acutesim::compute
