#include "platform_helper.h"
#include <QApplication>
#include <QScreen>
#include <QWindow>

namespace acutesim {
namespace gui {
namespace core {

bool PlatformHelper::isHighDPI() {
    auto screen = QApplication::primaryScreen();
    if (!screen) return false;
    return screen->devicePixelRatio() > 1.0;
}

double PlatformHelper::getScaleFactor(const QWidget* widget) {
    if (!widget) return 1.0;
    
    // Qt abstracted this well in recent versions, but for precise rendering 
    // sometimes we need the devicePixelRatioF
    if (auto window = widget->windowHandle()) {
        return window->devicePixelRatio();
    }
    // Fallback if window handle not created yet (e.g. constructor)
    if (auto screen = QApplication::primaryScreen()) {
        return screen->devicePixelRatio();
    }
    return 1.0; // Default
}

void PlatformHelper::applyWindowFlags(QMainWindow* window) {
#ifdef Q_OS_MAC
    // macOS: Unified title bar look
    window->setUnifiedTitleAndToolBarOnMac(true);
#endif
    // Windows/Linux specific flags can go here
}

int PlatformHelper::mmToPx(double mm) {
    auto screen = QApplication::primaryScreen();
    if (!screen) return static_cast<int>(mm * 3.78); // Fallback ~96 DPI
    return static_cast<int>(mm * screen->physicalDotsPerInch() / 25.4);
}

} // namespace core
} // namespace gui
} // namespace acutesim
