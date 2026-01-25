#pragma once

#include "physics_tensors.h"
#include <functional>
#include <iostream>
#include <vector>

namespace acutesim {
namespace compute {

enum class SimdArch {
    Generic,
    Neon,
    AVX2,
    AVX512,
    AVX10_256,
    AVX10_512
};

class KernelDispatcher {
public:
    using DiodeKernel = void(*)(DiodeTensor&, const std::vector<double>&);
    using MosfetKernel = void(*)(MosfetTensor&, const std::vector<double>&);
    using BJTKernel = void(*)(BJTTensor&, const std::vector<double>&);

    SimdArch architecture;
    DiodeKernel batch_diode_physics;
    MosfetKernel batch_mosfet_physics;
    BJTKernel batch_bjt_physics;

    static KernelDispatcher& get();

private:
    KernelDispatcher(); // Private CTR (Singleton)
    void detectCpuAndBind();
};

} // namespace compute
} // namespace acutesim
