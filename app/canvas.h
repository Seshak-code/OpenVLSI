#pragma once
// An editor's drawing surface: a Display (segments, filled squares, texts) in database units, y up, over a dot grid.
// Geometry draws on Qt's RHI (Metal on macOS); text on a QPainter layer above it (docs/rebuild.md, Rendering). Mouse
// presses come out in database units; trackpad scroll pans, pinch and ctrl/⌘-scroll zoom about the cursor, the
// middle button drags.
// ponytail: every frame re-sends all vertices and text paints with QPainter; fine for schematics (thousands of
// objects), not for layout (M3): retained vertex buffers per layer and a glyph atlas then.
#include "app/scene.h"

#include <QRhiWidget>

#include <memory>

class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;

namespace app {

class Canvas : public QRhiWidget {
    Q_OBJECT
public:
    explicit Canvas(QWidget* parent = nullptr);
    ~Canvas() override;

    void setDisplay(Display d);
    const Display& display() const { return display_; }
    void setGrid(double step) { grid_ = step; update(); }

    double scale() const { return scale_; }  // database units per pixel
    QPointF toWorld(QPointF px) const;
    QPointF toScreen(QPointF world) const;
    void fit(const QRectF& world);               // the whole of `world`, with a margin
    void zoomBy(double factor, QPointF aboutPx);  // > 1 zooms in
    void zoomBy(double factor) { zoomBy(factor, QPointF(width() / 2.0, height() / 2.0)); }
    void panBy(QPointF px);

signals:
    void pressed(QPointF world, Qt::KeyboardModifiers modifiers);
    void moved(QPointF world);
    void released(QPointF world);
    void doubleClicked(QPointF world);

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool event(QEvent* e) override;

private:
    friend class TextLayer;
    void changed();
    Display display_;
    QPointF center_{0, 0};
    double scale_ = 4, grid_ = 25;
    QWidget* text_;
    bool panning_ = false;
    QPointF last_;

    QRhi* rhi_ = nullptr;
    std::unique_ptr<QRhiBuffer> vbuf_, ubuf_;
    std::unique_ptr<QRhiShaderResourceBindings> srb_;
    std::unique_ptr<QRhiGraphicsPipeline> lines_, triangles_;
};

}  // namespace app
