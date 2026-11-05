#pragma once
// The layout viewer: a view's geometry through its hierarchy on a QRhi canvas, a layer panel, levels, rulers and
// selection. Geometry goes to the GPU once; panning and zooming change one matrix, so a routed block moves smoothly.
#include "app/session.h"
#include "layout/flatten.h"

#include <QMainWindow>
#include <QRhiWidget>
#include <QJsonObject>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>

class QLabel;
class QLineEdit;
class QDockWidget;
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
    std::vector<QRectF> selectedBoxes;                    // world: one per selected object
    std::vector<std::pair<QPointF, QPointF>> flightLines; // world: what remains to route
    std::vector<QRectF> markers;                          // world: what a check found, each a box
    int currentMarker = -1;
    enum class Rubber { None, Box, Line, Closed, Ghost };
    Rubber rubber = Rubber::None;                         // what the tool in progress draws to the cursor
    std::vector<QPointF> rubberPoints;
    std::vector<QRectF> ghost;                            // Ghost: boxes that follow the cursor from rubberPoints[0]
    void refreshOverlay();
    std::vector<std::pair<QPointF, QPointF>> rulers;
    std::optional<QPointF> rulerStart;
    QPointF cursor;
    double micronsPerDbu = 1e-3;

signals:
    void clicked(QPointF world, bool extend);
    void doubleClicked();
    void moved(QPointF world);

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
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
    bool dragged_ = false, swallowRelease_ = false;
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
    QJsonObject selectAt(QPointF world, bool extend = false);     // top coordinates relative to the origin; {} when nothing is there
    QJsonObject selectAtDbu(double x, double y);                  // in database units, absolute
    void zoomToMicrons(double x0, double y0, double x1, double y1);
    void startRuler();
    void clearRulers();

    // Editing: each tool gathers its clicks, snaps them to the process's grid and calls a command
    enum class Tool { Select, Ruler, Rect, Path, Polygon, Label, Pin, Via, Instance, Move, Copy, Stretch };
    void startTool(Tool t);
    Tool tool() const { return tool_; }
    void setToolText(const QString& text) { toolText_ = text; }  // what Label writes, what Pin names
    void setInstanceMaster(const QString& cell) { instanceMaster_ = cell; }
    void setCurrentLayer(int layer, int datatype);
    void clickDbu(double x, double y, bool extend = false);      // what a click there does, in the current tool
    void finishTool();                                           // Enter: ends a path or a polygon
    void cancelTool();                                           // Esc
    void deleteSelection();
    void undo();
    void redo();
    std::set<int64_t> selection() const { return selection_; }   // shape ids; an instance's as minus its id
    QString lastError() const { return lastError_; }
    int flightLineCount() const { return (int)canvas_->flightLines.size(); }
    void setFlightLinesShown(bool on);

    // What a DRC found (verify_markers), walked in a panel: choosing one shows it. Returns how many landed on this layout.
    int setMarkers(const QJsonObject& report);
    QTreeWidget* markerTree() const { return markerTree_; }
    void showMarker(int index);
    QRectF markerWindow(int index) const;
    QTreeWidget* layerTree() const { return layerTree_; }
    QString status() const;

private:
    void reload();
    void reloadIfChanged();
    void placeMarkers();
    void fillLayers();
    void buildStyles();
    void click(QPointF world, bool extend);
    void toolPoint(oadb::Point at);
    oadb::Point snapped(QPointF world) const;
    QPointF worldOf(oadb::Point p) const;
    void refreshSelection();
    void refreshFlightLines();
    bool run(const QString& command, QJsonObject args, QJsonValue* value = nullptr);  // the view's names added; false and lastError on a refusal
    void hint(const QString& text);
    bool currentIsMetal(QString* name) const;
    QString currentLayerName() const;
    void askPlaceInstance();
    void askTransistor();
    void askLabel(Tool t);
    Session& session_;
    QString lib_, cell_, view_;
    std::unique_ptr<layout::Hierarchy> hierarchy_;
    layout::Flat flat_;
    std::vector<LayerStyle> styles_;
    LayoutCanvas* canvas_;
    QTreeWidget* layerTree_;
    QTreeWidget* markerTree_;
    QDockWidget* markerDock_;
    QJsonObject markerReport_;
    std::vector<int> markerOfItem_;
    QLineEdit* layerFilter_;
    QLabel* status_;
    QLabel* position_;
    int levels_ = 1 << 30;
    bool rulerMode_ = false;
    Tool tool_ = Tool::Select;
    std::vector<oadb::Point> points_;      // the tool's clicks so far
    std::set<int64_t> selection_;
    std::map<layout::LayerKey, bool> shown_;
    std::optional<layout::LayerKey> current_;
    QString toolText_, instanceMaster_, lastError_;
    int64_t grid_ = 1, lastRevision_ = -1;
    bool flightShown_ = true;
    std::vector<int64_t> stretchShapes_;
    std::array<int64_t, 4> stretchWindow_{};
    bool stretchX_ = true, stretchY_ = true;
};

}  // namespace app
