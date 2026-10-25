#pragma once
// ============================================================================
// flow/eda_types.h
// Plain POD geometry types — no Qt dependency.
// ============================================================================

namespace acutesim::compute::orchestration {

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

struct Size2D {
    double width  = 0.0;
    double height = 0.0;
    bool isEmpty() const { return width <= 0.0 || height <= 0.0; }
};

struct Rect2D {
    double x      = 0.0;
    double y      = 0.0;
    double width  = 0.0;
    double height = 0.0;
};

} // namespace acutesim::compute::orchestration
