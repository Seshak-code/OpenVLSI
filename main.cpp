#include "benchmark_suite.h"
#include "mainwindow.h"
#include "monte_carlo.h"
#include "stress_test.h"
#include <iostream>
#include <string>
#include "gui/styling/theme_manager.h"

#include <QApplication>
#include <QScreen>

#ifdef __EMSCRIPTEN__
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

  if (argc > 1) {
    std::string arg = argv[1];
    if (arg == "--stress") {
      int numTrials = 100;
      if (argc > 2)
        numTrials = std::stoi(argv[2]);
      StressHarness::run(numTrials);
      return 0;
    } else if (arg == "--monte-carlo") {
      MonteCarloRunner::run();
      return 0;
    } else if (arg == "--benchmark") {
      GoldBench::run();
      return 0;
    }
  }

#ifndef __EMSCRIPTEN__
  // Force use of matching 6.9.0 plugins BEFORE QApplication constructor loads the platform plugin
  qputenv("QT_PLUGIN_PATH", "/opt/homebrew/opt/qt/share/qt/plugins");
  // Specifically force the platform plugin search path to avoid picking up qtbase 6.9.3's cocoa plugin
  qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", "/opt/homebrew/opt/qt/share/qt/plugins/platforms");
#endif

  QApplication app(argc, argv);
  
  // Explicit Version Log
  qDebug() << "==========================================";
  qDebug() << "   AcuteSim WASM - Build ID: 22264 (Fixed)";
  qDebug() << "==========================================";

  // Qt styling
  app.setStyle("Fusion");
  // Initialize ThemeManager with Dark Theme and apply
  acutesim::gui::styling::ThemeManager::instance().loadTheme("Dark");
  app.setStyleSheet(acutesim::gui::styling::ThemeManager::instance().getStyleSheet());

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
#else
  w.showMaximized();
#endif

  return app.exec();
}
