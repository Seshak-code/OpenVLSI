#pragma once
// The app's look, one place: colour tokens for light and dark, applied as a QPalette and a stylesheet. Dark is oxide
// (cool slate), light is bare wafer (cool grey); "via" cyan marks what is selected or in focus, and pass, warn and
// fail keep their meaning everywhere. Tools read their colours from Theme::current(), never from literals.
#include <QColor>
#include <QObject>
#include <QString>

class QApplication;

namespace app {

struct Theme {
    enum class Mode { Auto, Light, Dark };
    bool dark = true;
    QColor window, panel, field, raised, line, ink, muted, via, viaInk, pass, warn, fail;
    QColor canvas, canvasGrid, canvasInk;  // the schematic canvas and its plots

    static const Theme& current();
    static Mode mode();
    static QString modeName(Mode m);
    static bool parseMode(const QString& name, Mode* out);
    static void apply(QApplication& a, Mode m);  // sets the palette, stylesheet and current(); follows the OS in Auto
    static QObject* notifier();                  // emits changed() when the theme does
};

class ThemeNotifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void changed();
};

}  // namespace app
