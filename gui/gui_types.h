#pragma once

enum class AppMode { 
    LAB_BENCHMARK,     // Ideal sources, fast solver
    SYSTEM_INTEGRATION // Realistic sources (ESR/Ripple), high precision
};
