#pragma once
// The app's icons, drawn as vectors in the current theme's ink so they stay crisp at any scale and in either theme:
// a chip designer's glyphs rather than a stock set.
#include <QIcon>
#include <QString>

namespace app::icons {

// "library", "cell", "schematic", "symbol", "maestro", "waveform", "view", "design", "console", "command", "implement",
// "verify", "verifier", "layout", "add", "close"; an unknown name draws "view". active: in the accent colour.
QIcon get(const QString& name, bool active = false);

}  // namespace app::icons
