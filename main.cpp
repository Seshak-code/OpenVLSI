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
// Workaround for missing statx symbol in Emscripten 3.1.56+ vs Qt 6.9.3
struct statx;
extern "C" int statx(int dirfd, const char *pathname, int flags,
                     unsigned int mask, struct statx *statxbuf) {
  return -1; // ENOSYS
}
#endif

int main(int argc, char *argv[]) {
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

  QApplication app(argc, argv);

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
