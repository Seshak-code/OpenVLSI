#pragma once
// A waveform plot (ViVA's strip): traces y(x) over shared axes with SI-labelled ticks, a log x axis for frequency,
// two cursors (A: click, B: shift-click) and zoom (drag across x, the wheel; double-click fits). The y axis follows the
// visible x range.
#include <QColor>
#include <QWidget>

#include <cmath>
#include <optional>
#include <vector>

namespace app {

struct PlotTrace {
    QString name;
    QColor color;
    std::vector<double> x, y;
};

QString si(double v, int digits = 4);  // 1.5k, 10u, -3.2m

class Plot : public QWidget {
    Q_OBJECT
public:
    explicit Plot(QWidget* parent = nullptr);
    void setTraces(std::vector<PlotTrace> traces, bool logX, const QString& xLabel);  // fits
    const std::vector<PlotTrace>& traces() const { return traces_; }
    bool logX() const { return logX_; }
    void fit();
    void setXRange(double lo, double hi);
    std::pair<double, double> xRange() const { return {x0_, x1_}; }
    std::pair<double, double> yRange() const { return {y0_, y1_}; }
    std::optional<double> cursor(int k) const { return cursors_[k]; }  // 0 is A, 1 is B
    void setCursor(int k, double x);
    std::optional<double> valueAt(const PlotTrace& t, double x) const;  // between its points (in log x on a log axis)

signals:
    void cursorsChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    QRectF area() const;
    double u(double x) const { return logX_ ? std::log10(x) : x; }  // x on the axis' own scale
    double px(double x) const;
    double py(double y) const;
    double xAt(double px) const;
    void fitY();

    std::vector<PlotTrace> traces_;
    bool logX_ = false;
    QString xLabel_;
    double x0_ = 0, x1_ = 1, y0_ = 0, y1_ = 1;
    std::optional<double> cursors_[2];
    std::optional<QPointF> dragFrom_;
    QPointF dragTo_;
};

}  // namespace app
