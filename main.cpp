#include "mainwindow.h"
#include <iostream>
#include <string>
#include "gui/styling/theme_manager.h"
#ifndef __EMSCRIPTEN__
#include "acutesim_engine/infrastructure/hardware_capabilities.h"
#endif

#include <QApplication>
#include <QDir>
#include <QScreen>
#include <QTimer>

#ifdef __EMSCRIPTEN__
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWasmIntegrationPlugin)

#include <sys/stat.h>
#include <emscripten/emscripten.h>
#include "gui/core/storage_manager.h"
// Workaround for missing statx symbol in Emscripten 3.1.56+ vs Qt 6.9.3
struct statx;
extern "C" int statx(int dirfd, const char *pathname, int flags,
                     unsigned int mask, struct statx *statxbuf) {
  return -1; // ENOSYS
}

// Global debug helper for WASM
extern "C" EMSCRIPTEN_KEEPALIVE const char* dumpStorageFile(const char* projectName, const char* filePath) {
    using namespace acutesim::gui::core;
    QByteArray data = StorageManager::instance().loadFile(QString(projectName), QString(filePath));
    // Leak data for debug only - will return as JS string
    static QByteArray lastData;
    lastData = data;
    return lastData.constData();
}
#endif

int main(int argc, char *argv[]) {
  // Explicitly initialize Qt resources before QApplication
  Q_INIT_RESOURCE(icons);

#ifndef __EMSCRIPTEN__
  // Homebrew with both qt 6.9.0 and qtbase 6.9.3 installed: pick the matching plugins before QApplication loads the
  // platform plugin (else qtbase's cocoa plugin crashes it). Only when unset and that install exists.
  // ponytail: one machine's layout; a packaged build ships its own plugins
  if (const char* qtPlugins = "/opt/homebrew/opt/qt/share/qt/plugins";
      qEnvironmentVariableIsEmpty("QT_PLUGIN_PATH") && QDir(qtPlugins).exists()) {
    qputenv("QT_PLUGIN_PATH", qtPlugins);
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QByteArray(qtPlugins) + "/platforms");
  }
#endif

  QApplication app(argc, argv);

#ifndef __EMSCRIPTEN__
  {
      auto caps = acutesim::compute::detectCapabilities();
      std::cout << "[Engine] CPU: " << acutesim::compute::simdTierName(caps.simdTier)
                << " | Cores: " << caps.physicalCpuCores;
      if (caps.hasGPU)
          std::cout << " | GPU: " << caps.gpuAdapterName;
      std::cout << std::endl;
  }
#endif

  // Qt styling
  app.setStyle("Fusion");
  // Initialize ThemeManager with Dark Theme and apply
  auto& tm = acutesim::gui::styling::ThemeManager::instance();
  tm.loadTheme("Dark");
  app.setStyleSheet(tm.getStyleSheet());
  // Re-apply global stylesheet whenever the theme changes
  QObject::connect(&tm, &acutesim::gui::styling::ThemeManager::themeChanged,
                   &app, [&]() { app.setStyleSheet(tm.getStyleSheet()); });

  MainWindow w;

#ifdef __EMSCRIPTEN__
  // Wasm-specific: Force window to match screen interaction size
  // This allows the browser to handle scaling via the viewport
  auto screen = app.primaryScreen();
  if (screen) {
    const QRect geometry = screen->availableGeometry();
    w.setGeometry(geometry);
  }
  w.show();
  // Force a full repaint once the Emscripten event loop starts processing.
  // Without this, widgets created during construction are not painted until
  // a mouse hover triggers an update() on each one individually.
  // Use two-phase approach: process layout events first, then repaint every
  // visible child. w.repaint() only covers the top-level frame; findChildren
  // ensures dock contents, splitter panels, and tab widgets all get updated.
  QTimer::singleShot(0, [&w]() {
      QApplication::processEvents();
      const auto children = w.findChildren<QWidget*>();
      for (QWidget* child : children) {
          if (child->isVisible()) {
              child->update();
          }
      }
      w.update();
  });
#else
  w.showMaximized();
#endif

  return app.exec();
}
