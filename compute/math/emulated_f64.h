#pragma once
#include <cmath>
#include <iostream>
#include <limits>

/**
 * emulated_f64.h
 * 
 * Emulated Double Precision (Double-Float) for Tensor GPU translation verification.
 * 
 * Represents a high-precision number as a pair of floats (hi, lo).
 * On GPU this enables near-double precision using native float32 ALUs.
 * On CPU (here), it verifies that our physics kernels can operate on non-fundamental types.
 */

struct EmulatedF64 {
    float hi;
    float lo;

    // Constructors
    constexpr EmulatedF64() : hi(0.0f), lo(0.0f) {}
    constexpr EmulatedF64(float h, float l) : hi(h), lo(l) {}
    
    // Explicit conversion from double (for seeding/constants)
    // In a real DS implementation, we would split the double.
    // Here we can capture the value as best as possible.
    EmulatedF64(double val) {
        hi = static_cast<float>(val);
        lo = static_cast<float>(val - static_cast<double>(hi));
    }
    
    // Convert back to double for interaction with legacy code/constants
    explicit operator double() const {
        return static_cast<double>(hi) + static_cast<double>(lo);
    }
    
    // Assignment from scalar (double literals in code)
    EmulatedF64& operator=(double val) {
        *this = EmulatedF64(val);
        return *this;
    }

    // Unary operators
    EmulatedF64 operator-() const {
        return {-hi, -lo};
    }
    
    // Arithmetic operators
    // We implement these using double intermediate to simulate the "result"
    // In actual GPU code, this would use Dekker/Knuth specific float ops.
    // For translation verification, matching the semantic result is sufficient.
    
    EmulatedF64 operator+(const EmulatedF64& other) const {
        return EmulatedF64((double)*this + (double)other);
    }
    EmulatedF64 operator-(const EmulatedF64& other) const {
        return EmulatedF64((double)*this - (double)other);
    }
    EmulatedF64 operator*(const EmulatedF64& other) const {
        return EmulatedF64((double)*this * (double)other);
    }
    EmulatedF64 operator/(const EmulatedF64& other) const {
        return EmulatedF64((double)*this / (double)other);
    }
    
    // Compound assignment
    EmulatedF64& operator+=(const EmulatedF64& rhs) { *this = *this + rhs; return *this; }
    EmulatedF64& operator-=(const EmulatedF64& rhs) { *this = *this - rhs; return *this; }
    EmulatedF64& operator*=(const EmulatedF64& rhs) { *this = *this * rhs; return *this; }
    EmulatedF64& operator/=(const EmulatedF64& rhs) { *this = *this / rhs; return *this; }
    
    // Comparisons
    bool operator<(const EmulatedF64& rhs) const { return (double)*this < (double)rhs; }
    bool operator>(const EmulatedF64& rhs) const { return (double)*this > (double)rhs; }
    bool operator<=(const EmulatedF64& rhs) const { return (double)*this <= (double)rhs; }
    bool operator>=(const EmulatedF64& rhs) const { return (double)*this >= (double)rhs; }
    bool operator==(const EmulatedF64& rhs) const { return (double)*this == (double)rhs; }
    bool operator!=(const EmulatedF64& rhs) const { return (double)*this != (double)rhs; }
};

// Math overloads matching ADL expectations
// These must be in the global namespace (or same namespace as EmulatedF64)
// to be found by unqualified exp(val).

inline EmulatedF64 exp(const EmulatedF64& x) {
    return EmulatedF64(std::exp((double)x));
}

inline EmulatedF64 log(const EmulatedF64& x) {
    return EmulatedF64(std::log((double)x));
}

inline EmulatedF64 pow(const EmulatedF64& b, const EmulatedF64& e) {
    return EmulatedF64(std::pow((double)b, (double)e));
}

inline EmulatedF64 abs(const EmulatedF64& x) {
    return EmulatedF64(std::abs((double)x));
}

inline EmulatedF64 sqrt(const EmulatedF64& x) {
    return EmulatedF64(std::sqrt((double)x));
}

inline EmulatedF64 min(const EmulatedF64& a, const EmulatedF64& b) {
    return (double)a < (double)b ? a : b;
}

inline EmulatedF64 max(const EmulatedF64& a, const EmulatedF64& b) {
    return (double)a > (double)b ? a : b;
}

// Support for mixed arithmetic with double/float literals
inline EmulatedF64 operator+(double lhs, const EmulatedF64& rhs) { return EmulatedF64(lhs) + rhs; }
inline EmulatedF64 operator+(const EmulatedF64& lhs, double rhs) { return lhs + EmulatedF64(rhs); }

inline EmulatedF64 operator-(double lhs, const EmulatedF64& rhs) { return EmulatedF64(lhs) - rhs; }
inline EmulatedF64 operator-(const EmulatedF64& lhs, double rhs) { return lhs - EmulatedF64(rhs); }

inline EmulatedF64 operator*(double lhs, const EmulatedF64& rhs) { return EmulatedF64(lhs) * rhs; }
inline EmulatedF64 operator*(const EmulatedF64& lhs, double rhs) { return lhs * EmulatedF64(rhs); }

inline EmulatedF64 operator/(double lhs, const EmulatedF64& rhs) { return EmulatedF64(lhs) / rhs; }
inline EmulatedF64 operator/(const EmulatedF64& lhs, double rhs) { return lhs / EmulatedF64(rhs); }

// Stream output
inline std::ostream& operator<<(std::ostream& os, const EmulatedF64& val) {
    os << (double)val;
    return os;
}

// Numeric limits specialization
namespace std {
    template<> class numeric_limits<EmulatedF64> {
    public:
        static constexpr bool is_specialized = true;
        static constexpr bool is_signed = true;
        static constexpr bool is_integer = false;
        static constexpr bool is_exact = false;
        static constexpr bool has_infinity = true;
        static constexpr bool has_quiet_NaN = true;
        
        static EmulatedF64 epsilon() { return EmulatedF64(1e-14); } // Roughly double precision
        static EmulatedF64 min() { return EmulatedF64(std::numeric_limits<float>::min()); }
        static EmulatedF64 max() { return EmulatedF64(std::numeric_limits<float>::max()); }
        static EmulatedF64 infinity() { return EmulatedF64(std::numeric_limits<float>::infinity()); }
        static EmulatedF64 quiet_NaN() { return EmulatedF64(std::numeric_limits<float>::quiet_NaN()); }
    };
    
    // Allow isfinite check if used via std::
    inline bool isfinite(const EmulatedF64& x) {
        return std::isfinite((double)x);
    }
}
