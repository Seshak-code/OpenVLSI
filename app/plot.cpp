#include "app/plot.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <climits>
#include <cmath>

namespace app {

QString si(double v, int digits) {
    if (v == 0 || !std::isfinite(v)) return QString::number(v);
    static const std::pair<double, const char*> prefixes[] = {{1e12, "T"}, {1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1, ""}, {1e-3, "m"},
                                                              {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"}, {1e-18, "a"}};
    for (const auto& [m, p] : prefixes)
        if (std::abs(v) >= m * 0.9999999) return QString::number(v / m, 'g', digits) + p;
    return QString::number(v, 'g', digits);
}

namespace {
// ticks a "nice" step apart (1, 2 or 5 times a power of ten) across [lo, hi]
std::vector<double> ticks(double lo, double hi) {
    std::vector<double> out;
    if (!(hi > lo)) return out;
    const double raw = (hi - lo) / 6, mag = std::pow(10, std::floor(std::log10(raw)));
    const double step = mag * (raw / mag >= 5 ? 5 : raw / mag >= 2 ? 2 : 1);
    for (double t = std::ceil(lo / step) * step; t <= hi + step * 1e-9; t += step) out.push_back(std::abs(t) < step * 1e-9 ? 0 : t);
    return out;
}
}  // namespace

Plot::Plot(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 200);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
}

void Plot::setTraces(std::vector<PlotTrace> traces, bool logX, const QString& xLabel) {
    traces_ = std::move(traces);
    logX_ = logX;
    xLabel_ = xLabel;
    fit();
}

void Plot::fit() {
    bool any = false;
    double lo = 0, hi = 1;
    for (const auto& t : traces_)
        for (const double x : t.x) {
            if (logX_ && x <= 0) continue;  // a log axis shows the positive part
            lo = any ? std::min(lo, x) : x, hi = any ? std::max(hi, x) : x, any = true;
        }
    if (any && hi == lo) lo -= 0.5, hi += 0.5;
    setXRange(lo, hi);
}

void Plot::setXRange(double lo, double hi) {
    if (lo > hi) std::swap(lo, hi);
    if (logX_ && lo <= 0) lo = hi > 0 ? hi / 1e6 : 1;
    if (hi == lo) return;
    x0_ = lo, x1_ = hi;
    fitY();
    update();
}

void Plot::fitY() {  // to what is visible in x
    bool any = false;
    double lo = 0, hi = 1;
    for (const auto& t : traces_)
        for (size_t k = 0; k < t.x.size(); ++k) {
            if (t.x[k] < x0_ || t.x[k] > x1_ || !std::isfinite(t.y[k])) continue;
            lo = any ? std::min(lo, t.y[k]) : t.y[k], hi = any ? std::max(hi, t.y[k]) : t.y[k], any = true;
        }
    const double pad = hi > lo ? (hi - lo) * 0.05 : std::max(std::abs(hi) * 0.05, 1e-12);
    y0_ = lo - pad, y1_ = hi + pad;
}

void Plot::setCursor(int k, double x) {
    cursors_[k] = x;
    update();
    emit cursorsChanged();
}

std::optional<double> Plot::valueAt(const PlotTrace& t, double x) const {
    for (size_t k = 1; k < t.x.size(); ++k) {
        const double a = u(t.x[k - 1]), b = u(t.x[k]), v = u(x);
        if ((v - a) * (v - b) > 0) continue;
        const double f = a == b ? 0 : (v - a) / (b - a);
        return t.y[k - 1] + f * (t.y[k] - t.y[k - 1]);
    }
    return std::nullopt;
}

QRectF Plot::area() const { return QRectF(70, 12, std::max(10, width() - 90), std::max(10, height() - 52)); }
double Plot::px(double x) const { return area().left() + (u(x) - u(x0_)) / (u(x1_) - u(x0_)) * area().width(); }
double Plot::py(double y) const { return area().bottom() - (y - y0_) / (y1_ - y0_) * area().height(); }
double Plot::xAt(double p) const {
    const double v = u(x0_) + (p - area().left()) / area().width() * (u(x1_) - u(x0_));
    return logX_ ? std::pow(10, v) : v;
}

void Plot::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF a = area();
    p.fillRect(rect(), palette().base());
    const QColor grid = palette().mid().color(), ink = palette().text().color();
    p.setFont(QFont(font().family(), 10));

    // grid and tick labels: decades (and 2..9) on a log axis
    std::vector<double> xt;
    if (logX_) {
        for (double d = std::floor(std::log10(x0_)); d <= std::ceil(std::log10(x1_)); ++d)
            for (int m = 1; m < 10; ++m) if (const double x = m * std::pow(10, d); x >= x0_ && x <= x1_) xt.push_back(x);
    } else {
        xt = ticks(x0_, x1_);
    }
    for (const double x : xt) {
        const bool major = !logX_ || std::abs(std::log10(x) - std::round(std::log10(x))) < 1e-9;
        p.setPen(QPen(grid, major ? 1 : 0.5, major ? Qt::SolidLine : Qt::DotLine));
        p.drawLine(QPointF(px(x), a.top()), QPointF(px(x), a.bottom()));
        if (major) {
            p.setPen(ink);
            p.drawText(QRectF(px(x) - 40, a.bottom() + 4, 80, 16), Qt::AlignHCenter | Qt::AlignTop, si(x, 3));
        }
    }
    for (const double y : ticks(y0_, y1_)) {
        p.setPen(QPen(grid, 1));
        p.drawLine(QPointF(a.left(), py(y)), QPointF(a.right(), py(y)));
        p.setPen(ink);
        p.drawText(QRectF(0, py(y) - 8, a.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, si(y, 3));
    }
    p.drawText(QRectF(a.left(), a.bottom() + 20, a.width(), 16), Qt::AlignHCenter, xLabel_ + (logX_ ? " (log)" : ""));
    p.setPen(QPen(ink, 1));
    p.drawRect(a);

    // traces: at most two points per pixel column (the column's extremes) once a trace is denser than the screen
    // ponytail: rebuilt every paint; cache per trace and x range when traces reach 10^7 points
    p.setClipRect(a);
    for (const auto& t : traces_) {
        QPainterPath path;
        bool started = false;
        int col = INT_MIN;
        double lo = 0, hi = 0;
        auto flush = [&] {
            if (col == INT_MIN) return;
            const QPointF l(col, py(lo)), h(col, py(hi));
            started ? path.lineTo(l) : path.moveTo(l);
            path.lineTo(h);
            started = true;
        };
        for (size_t k = 0; k < t.x.size(); ++k) {
            if ((logX_ && t.x[k] <= 0) || !std::isfinite(t.y[k])) continue;
            const int c = (int)std::floor(px(t.x[k]));
            if (c != col) {
                flush();
                col = c, lo = hi = t.y[k];
            } else {
                lo = std::min(lo, t.y[k]), hi = std::max(hi, t.y[k]);
            }
        }
        flush();
        p.setPen(QPen(t.color, 1.6));
        p.drawPath(path);
    }
    for (int k = 0; k < 2; ++k)
        if (cursors_[k] && *cursors_[k] >= x0_ && *cursors_[k] <= x1_) {
            const QColor c = k ? QColor(40, 110, 230) : QColor(220, 50, 50);
            p.setPen(QPen(c, 1, Qt::DashLine));
            p.drawLine(QPointF(px(*cursors_[k]), a.top()), QPointF(px(*cursors_[k]), a.bottom()));
            p.drawText(QPointF(px(*cursors_[k]) + 3, a.top() + 12), k ? "B" : "A");
        }
    if (dragFrom_) p.fillRect(QRectF(QPointF(dragFrom_->x(), a.top()), QPointF(dragTo_.x(), a.bottom())).normalized(), QColor(120, 120, 255, 50));
}

void Plot::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) dragFrom_ = e->position(), dragTo_ = e->position();
}

void Plot::mouseMoveEvent(QMouseEvent* e) {
    if (dragFrom_) {
        dragTo_ = e->position();
        update();
    }
}

void Plot::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragFrom_ || e->button() != Qt::LeftButton) return;
    const QPointF from = *dragFrom_;
    dragFrom_.reset();
    if (std::abs(e->position().x() - from.x()) > 5) setXRange(xAt(from.x()), xAt(e->position().x()));  // a drag zooms
    else setCursor(e->modifiers() & Qt::ShiftModifier ? 1 : 0, xAt(e->position().x()));  // a click places a cursor
    update();
}

void Plot::mouseDoubleClickEvent(QMouseEvent*) { fit(); }

void Plot::wheelEvent(QWheelEvent* e) {
    const double f = std::pow(1.0015, -e->angleDelta().y()), at = u(xAt(e->position().x()));
    const double lo = at + (u(x0_) - at) * f, hi = at + (u(x1_) - at) * f;
    setXRange(logX_ ? std::pow(10, lo) : lo, logX_ ? std::pow(10, hi) : hi);
    e->accept();
}

}  // namespace app
