#pragma once
// The layout viewer: a view's geometry through its hierarchy on a QRhi canvas, a layer panel, levels, rulers and
// selection. Geometry goes to the GPU once; panning and zooming change one matrix, so a routed block moves smoothly.
#include "app/session.h"
#include "layout/flatten.h"

#include <QMainWindow>
#include <QRhiWidget>
#include <QJsonObject>

#include <memory>
#include <optional>

class QLabel;
class QLineEdit;
class QTreeWidget;
class QRhi;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;

namespace app {

struct LayerStyle {
    QString name;
    QColor color;
    bool visible = true;
};

class LayoutOverlay;

class LayoutCanvas : public QRhiWidget {
    Q_OBJECT
public:
    explicit LayoutCanvas(QWidget* parent = nullptr);
    ~LayoutCanvas() override;
    void setScene(const layout::Flat* flat, const std::vector<LayerStyle>* styles);
    void geometryChanged();          // colours or the scene: re-upload
    void visibilityChanged();
    void fit();
    void zoomTo(double x0, double y0, double x1, double y1);  // absolute database units
    QPointF toWorld(QPointF px) const;  // relative to the scene's origin
    QPointF toScreen(QPointF world) const;
    double scale() const { return scale_; }  // pixels per database unit
    void zoomBy(double factor, QPointF aboutPx);
    void panBy(QPointF px);
    const layout::Flat* flat() const { return flat_; }
    const std::vector<LayerStyle>* styles() const { return styles_; }
    std::optional<size_t> selected;
    std::vector<std::pair<QPointF, QPointF>> rulers;
    std::optional<QPointF> rulerStart;
    QPointF cursor;
    double micronsPerDbu = 1e-3;

signals:
    void clicked(QPointF world);
    void moved(QPointF world);

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    bool event(QEvent* e) override;

private:
    void changed();
    const layout::Flat* flat_ = nullptr;
    const std::vector<LayerStyle>* styles_ = nullptr;
    QRhi* rhi_ = nullptr;
    std::unique_ptr<QRhiBuffer> ubuf_, lineBuf_, vbuf_;
    std::unique_ptr<QRhiShaderResourceBindings> srb_, lineSrb_;
    std::unique_ptr<QRhiGraphicsPipeline> lines_, triangles_;
    struct Range { quint32 first = 0, count = 0; };
    std::vector<Range> fills_, outlines_;
    bool dirty_ = true, moved_ = false;
    double scale_ = 1, cx_ = 0, cy_ = 0;
    std::optional<QPoint> dragFrom_;
    QPoint pressAt_;
    bool dragged_ = false;
    LayoutOverlay* overlay_;
};

class LayoutViewer : public QMainWindow {
    Q_OBJECT
public:
    LayoutViewer(Session& session, const QString& lib, const QString& cell, const QString& view, QWidget* parent = nullptr);
    ~LayoutViewer() override;
    QString library() const { return lib_; }
    QString cell() const { return cell_; }
    QString view() const { return view_; }
    LayoutCanvas* canvas() const { return canvas_; }
    const layout::Flat& flat() const { return flat_; }

    bool setLayerVisible(int layer, int datatype, bool visible);  // false: the design has no such layer
    void setLevels(int levels);                                   // how deep shapes are drawn; 0 the top cell
    int levels() const { return levels_; }
    QJsonObject selectAt(QPointF world);                          // top coordinates relative to the origin; {} when nothing is there
    QJsonObject selectAtDbu(double x, double y);                  // in database units, absolute
    void zoomToMicrons(double x0, double y0, double x1, double y1);
    void startRuler();
    void clearRulers();
    QTreeWidget* layerTree() const { return layerTree_; }
    QString status() const;

private:
    void reload();
    void fillLayers();
    void buildStyles();
    Session& session_;
    QString lib_, cell_, view_;
    std::unique_ptr<layout::Hierarchy> hierarchy_;
    layout::Flat flat_;
    std::vector<LayerStyle> styles_;
    LayoutCanvas* canvas_;
    QTreeWidget* layerTree_;
    QLineEdit* layerFilter_;
    QLabel* status_;
    QLabel* position_;
    int levels_ = 1 << 30;
    bool rulerMode_ = false;
};

}  // namespace app
