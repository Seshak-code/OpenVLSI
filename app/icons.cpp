#include "app/icons.h"

#include "app/theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace app::icons {

namespace {
// On a 24 x 24 grid
void draw(QPainter& p, const QString& n) {
    QPainterPath a;
    if (n == "schematic" || n == "design") {  // a MOSFET: gate, channel, source and drain
        a.moveTo(4, 12); a.lineTo(10, 12);
        a.moveTo(10, 7); a.lineTo(10, 17);
        a.moveTo(13, 6); a.lineTo(13, 18);
        a.moveTo(13, 8); a.lineTo(20, 8); a.lineTo(20, 3);
        a.moveTo(13, 16); a.lineTo(20, 16); a.lineTo(20, 21);
    } else if (n == "symbol") {  // an amplifier's triangle
        a.moveTo(6, 4); a.lineTo(19, 12); a.lineTo(6, 20); a.closeSubpath();
        a.moveTo(2, 9); a.lineTo(6, 9);
        a.moveTo(2, 15); a.lineTo(6, 15);
        a.moveTo(19, 12); a.lineTo(23, 12);
    } else if (n == "test") {  // a chip on its probes: a package, and a pulse through it
        a.addRoundedRect(QRectF(6, 6, 12, 12), 1.5, 1.5);
        for (int k = 0; k < 3; ++k) {
            const qreal at = 9 + 3 * k;
            a.moveTo(at, 3); a.lineTo(at, 6);
            a.moveTo(at, 18); a.lineTo(at, 21);
            a.moveTo(3, at); a.lineTo(6, at);
            a.moveTo(18, at); a.lineTo(21, at);
        }
        a.moveTo(8, 14); a.lineTo(10.5, 14); a.lineTo(10.5, 10); a.lineTo(13.5, 10); a.lineTo(13.5, 14); a.lineTo(16, 14);
    } else if (n == "verifier") {  // requirements, ticked off
        for (int k = 0; k < 3; ++k) {
            const qreal y = 6 + 6 * k;
            a.moveTo(3, y); a.lineTo(5, y + 2); a.lineTo(8.5, y - 2);
            a.moveTo(11, y); a.lineTo(21, y);
        }
    } else if (n == "maestro") {  // test setup: sliders
        for (int k = 0; k < 3; ++k) {
            const qreal y = 6 + 6 * k, x = k == 1 ? 8 : k == 0 ? 15 : 12;
            a.moveTo(3, y); a.lineTo(x - 2, y);
            a.addEllipse(QPointF(x, y), 2, 2);
            a.moveTo(x + 2, y); a.lineTo(21, y);
        }
    } else if (n == "waveform") {  // a settling step response over its axes
        a.moveTo(3, 3); a.lineTo(3, 21); a.lineTo(21, 21);
        a.moveTo(5, 17); a.lineTo(8, 17);
        a.cubicTo(9, 6, 11, 5, 13, 9);
        a.cubicTo(14, 11, 16, 11, 21, 10);
    } else if (n == "library") {  // books on a shelf
        a.addRect(4, 4, 4, 16);
        a.addRect(10, 4, 4, 16);
        a.moveTo(16.5, 4.7); a.lineTo(20.2, 4); a.lineTo(22, 19.3); a.lineTo(18.4, 20); a.closeSubpath();
    } else if (n == "cell") {  // a die with its pads
        a.addRect(6, 6, 12, 12);
        for (qreal t : {9.0, 15.0}) {
            a.moveTo(t, 2.5); a.lineTo(t, 6);
            a.moveTo(t, 18); a.lineTo(t, 21.5);
            a.moveTo(2.5, t); a.lineTo(6, t);
            a.moveTo(18, t); a.lineTo(21.5, t);
        }
    } else if (n == "implement" || n == "layout") {  // a layer stack
        for (int k = 0; k < 3; ++k) {
            const qreal y = 7 + 5 * k;
            a.moveTo(12, y - 4); a.lineTo(21, y); a.lineTo(12, y + 4); a.lineTo(3, y); a.closeSubpath();
        }
    } else if (n == "verify") {  // a check in a DRC marker
        a.addRect(4, 4, 16, 16);
        a.moveTo(8, 12); a.lineTo(11, 15); a.lineTo(16, 9);
    } else if (n == "console") {  // a prompt
        a.moveTo(4, 7); a.lineTo(9, 12); a.lineTo(4, 17);
        a.moveTo(11, 18); a.lineTo(20, 18);
    } else if (n == "command") {  // a search lens
        a.addEllipse(QPointF(10.5, 10.5), 6, 6);
        a.moveTo(15, 15); a.lineTo(21, 21);
    } else if (n == "close") {
        a.moveTo(7, 7); a.lineTo(17, 17);
        a.moveTo(17, 7); a.lineTo(7, 17);
    } else if (n == "add") {
        a.moveTo(12, 5); a.lineTo(12, 19);
        a.moveTo(5, 12); a.lineTo(19, 12);
    } else {  // a sheet
        a.moveTo(6, 3); a.lineTo(14, 3); a.lineTo(19, 8); a.lineTo(19, 21); a.lineTo(6, 21); a.closeSubpath();
        a.moveTo(14, 3); a.lineTo(14, 8); a.lineTo(19, 8);
    }
    p.drawPath(a);
}
}  // namespace

QIcon get(const QString& name, bool active) {
    const Theme& t = Theme::current();
    QIcon icon;
    for (const qreal ratio : {1.0, 2.0}) {
        for (const int size : {16, 24}) {
            QPixmap px(QSize(size, size) * ratio);
            px.setDevicePixelRatio(ratio);
            px.fill(Qt::transparent);
            QPainter p(&px);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(size / 24.0, size / 24.0);
            p.setPen(QPen(active ? t.via : t.muted, size == 16 ? 2.1 : 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            draw(p, name);
            icon.addPixmap(px);
        }
    }
    return icon;
}

}  // namespace app::icons
