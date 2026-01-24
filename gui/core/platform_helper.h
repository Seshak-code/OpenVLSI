#pragma once

#include <QObject>
#include <QWidget>
#include <QMainWindow>

namespace acutesim {
namespace gui {
namespace core {

class PlatformHelper {
public:
    // DPI & Scaling
    static bool isHighDPI();
    static double getScaleFactor(const QWidget* widget);
    
    // Window Management
    static void applyWindowFlags(QMainWindow* window);
    
    // Metric Conversion
    // Converts logical millimeters to device pixels approx.
    static int mmToPx(double mm);
    
private:
    PlatformHelper() = delete; // Static only
};

} // namespace core
} // namespace gui
} // namespace acutesim
