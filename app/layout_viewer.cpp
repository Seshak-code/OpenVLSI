#include "app/layout_viewer.h"

#include "app/palette.h"
#include "app/theme.h"

#include <QAction>
#include <QDockWidget>
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPolygonF>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace app {

namespace {
constexpr int kVertex = 12;  // x, y, rgba8
constexpr int kUniforms = 80;  // mvp, opacity (std140 pads it)

QShader shader(const QString& name) {
    QFile f(name);
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}

// A colour for a layer the design's tech does not name: evenly spread hues, stable per layer
QColor spread(const layout::LayerKey& k) {
    const int hue = (k.first * 137 + k.second * 61) % 360;
    return QColor::fromHsv(hue, 150, Theme::current().dark ? 235 : 170);
}

struct FlatLabelPick { const layout::FlatLabel* label = nullptr; };
FlatLabelPick nearestLabel(const layout::Flat& flat, const std::vector<LayerStyle>& styles, QPointF w, double reach) {
    FlatLabelPick out;
    double best = reach;
    for (const auto& l : flat.labels) {
        if (!styles[l.layer].visible) continue;
        const double d = std::hypot(l.x - w.x(), l.y - w.y());
        if (d <= best) {
            best = d;
            out.label = &l;
        }
    }
    return out;
}

QString engineering(double um) {
    if (std::abs(um) >= 1000) return QString::number(um / 1000, 'f', 3) + " mm";
    if (std::abs(um) >= 1) return QString::number(um, 'f', 3) + " µm";
    return QString::number(um * 1000, 'f', 1) + " nm";
}
}  // namespace

// Labels, the selection, rulers and level-limited masters, drawn over the geometry in screen space
class LayoutOverlay : public QWidget {
public:
    explicit LayoutOverlay(LayoutCanvas* c) : QWidget(c), c_(c) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const layout::Flat* flat = c_->flat();
        if (!flat) return;
        const Theme& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        p.setPen(QPen(t.muted, 1));
        for (size_t k = 0; k + 4 <= flat->boxes.size(); k += 4) {
            const QRectF r(c_->toScreen({flat->boxes[k], flat->boxes[k + 3]}), c_->toScreen({flat->boxes[k + 2], flat->boxes[k + 1]}));
            if (r.intersects(rect())) p.drawRect(r);
        }

        // Labels, once few enough are in view to read
        const QRectF view = rect();
        std::vector<std::pair<QPointF, const layout::FlatLabel*>> shown;
        for (const auto& l : flat->labels) {
            if (c_->styles() && !(*c_->styles())[l.layer].visible) continue;
            const QPointF at = c_->toScreen({l.x, l.y});
            if (!view.contains(at)) continue;
            shown.push_back({at, &l});
            if (shown.size() > 300) break;
        }
        if (shown.size() <= 300) {
            QFont f = font();
            f.setPixelSize(11);
            p.setFont(f);
            for (const auto& [at, l] : shown) {
                p.setPen(c_->styles() ? (*c_->styles())[l->layer].color : t.ink);
                p.drawText(at + QPointF(3, -3), QString::fromStdString(l->text));
            }
        }

        p.setPen(QPen(t.via, 2));
        for (const QRectF& s : c_->selectedBoxes) p.drawRect(QRectF(c_->toScreen(s.topLeft()), c_->toScreen(s.bottomRight())).normalized());

        for (int k = 0; k < (int)c_->markers.size(); ++k) {
            const QRectF box = QRectF(c_->toScreen(c_->markers[k].topLeft()), c_->toScreen(c_->markers[k].bottomRight())).normalized();
            if (!box.adjusted(-8, -8, 8, 8).intersects(rect())) continue;
            const bool current = k == c_->currentMarker;
            p.setPen(QPen(t.fail, current ? 2.5 : 1));
            p.setBrush(Qt::NoBrush);
            // A marker smaller than a few pixels still shows: a cross at its place
            if (box.width() < 6 && box.height() < 6) {
                const QPointF at = box.center();
                p.drawLine(at + QPointF(-4, -4), at + QPointF(4, 4));
                p.drawLine(at + QPointF(-4, 4), at + QPointF(4, -4));
            } else {
                p.drawRect(box);
            }
        }

        if (!c_->flightLines.empty()) {
            QPen line(t.warn, 1);
            line.setStyle(Qt::DashLine);
            p.setPen(line);
            for (const auto& [a, b] : c_->flightLines) p.drawLine(c_->toScreen(a), c_->toScreen(b));
        }

        if (c_->rubber != LayoutCanvas::Rubber::None && !c_->rubberPoints.empty()) {
            p.setPen(QPen(t.via, 1));
            const QPointF to = c_->toScreen(c_->cursor);
            if (c_->rubber == LayoutCanvas::Rubber::Box) {
                p.drawRect(QRectF(c_->toScreen(c_->rubberPoints[0]), to).normalized());
            } else if (c_->rubber == LayoutCanvas::Rubber::Ghost) {
                const QPointF by = c_->cursor - c_->rubberPoints[0];
                p.drawLine(c_->toScreen(c_->rubberPoints[0]), to);
                for (const QRectF& g : c_->ghost) p.drawRect(QRectF(c_->toScreen(g.topLeft() + by), c_->toScreen(g.bottomRight() + by)).normalized());
            } else {
                QPolygonF line;
                for (const QPointF& w : c_->rubberPoints) line << c_->toScreen(w);
                line << to;
                if (c_->rubber == LayoutCanvas::Rubber::Closed) line << c_->toScreen(c_->rubberPoints[0]);
                p.drawPolyline(line);
            }
        }

        p.setRenderHint(QPainter::Antialiasing);
        const auto ruler = [&](QPointF a, QPointF b) {
            const QPointF sa = c_->toScreen(a), sb = c_->toScreen(b);
            p.setPen(QPen(t.warn, 1));
            p.drawLine(sa, sb);
            for (const QPointF& e : {sa, sb}) p.drawEllipse(e, 2.5, 2.5);
            const double dx = (b.x() - a.x()) * c_->micronsPerDbu, dy = (b.y() - a.y()) * c_->micronsPerDbu;
            QFont f = font();
            f.setPixelSize(11);
            p.setFont(f);
            p.drawText((sa + sb) / 2 + QPointF(5, -5), engineering(std::hypot(dx, dy)));
        };
        for (const auto& [a, b] : c_->rulers) ruler(a, b);
        if (c_->rulerStart) ruler(*c_->rulerStart, c_->cursor);
    }

private:
    LayoutCanvas* c_;
};

LayoutCanvas::LayoutCanvas(QWidget* parent) : QRhiWidget(parent), overlay_(new LayoutOverlay(this)) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 150);
    connect(static_cast<ThemeNotifier*>(Theme::notifier()), &ThemeNotifier::changed, this, [this] { changed(); });
}

LayoutCanvas::~LayoutCanvas() = default;

void LayoutCanvas::changed() {
    update();
    overlay_->update();
}

void LayoutCanvas::setScene(const layout::Flat* flat, const std::vector<LayerStyle>* styles) {
    flat_ = flat;
    styles_ = styles;
    dirty_ = true;
    if (!moved_) fit();
    changed();
}

void LayoutCanvas::geometryChanged() {
    dirty_ = true;
    changed();
}

void LayoutCanvas::visibilityChanged() { changed(); }

void LayoutCanvas::fit() {
    if (!flat_ || flat_->box.empty()) return;
    const double w = flat_->box.x1 - flat_->box.x0, h = flat_->box.y1 - flat_->box.y0;
    scale_ = 0.92 * std::min(width() / std::max(w, 1.0), height() / std::max(h, 1.0));
    cx_ = (flat_->box.x0 + flat_->box.x1) / 2 - flat_->originX;
    cy_ = (flat_->box.y0 + flat_->box.y1) / 2 - flat_->originY;
    moved_ = false;
    changed();
}

void LayoutCanvas::zoomTo(double x0, double y0, double x1, double y1) {
    if (!flat_) return;
    const double w = std::max(x1 - x0, 1.0), h = std::max(y1 - y0, 1.0);
    scale_ = std::min(width() / w, height() / h);
    cx_ = (x0 + x1) / 2 - flat_->originX;
    cy_ = (y0 + y1) / 2 - flat_->originY;
    moved_ = true;
    changed();
}

QPointF LayoutCanvas::toWorld(QPointF px) const {
    return {cx_ + (px.x() - width() / 2.0) / scale_, cy_ - (px.y() - height() / 2.0) / scale_};
}

QPointF LayoutCanvas::toScreen(QPointF w) const {
    return {width() / 2.0 + (w.x() - cx_) * scale_, height() / 2.0 - (w.y() - cy_) * scale_};
}

void LayoutCanvas::zoomBy(double factor, QPointF about) {
    const QPointF before = toWorld(about);
    moved_ = true;
    scale_ *= factor;
    const QPointF after = toWorld(about);
    cx_ += before.x() - after.x();
    cy_ += before.y() - after.y();
    changed();
}

void LayoutCanvas::panBy(QPointF px) {
    moved_ = true;
    cx_ -= px.x() / scale_;
    cy_ += px.y() / scale_;
    changed();
}

void LayoutCanvas::resizeEvent(QResizeEvent* e) {
    QRhiWidget::resizeEvent(e);
    overlay_->setGeometry(rect());
    if (!moved_) fit();
}

void LayoutCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();
    dragFrom_ = e->position().toPoint();
    pressAt_ = dragFrom_.value();
    dragged_ = false;
}

void LayoutCanvas::mouseMoveEvent(QMouseEvent* e) {
    cursor = toWorld(e->position());
    emit moved(cursor);
    if (dragFrom_) {
        const QPoint at = e->position().toPoint();
        if ((at - pressAt_).manhattanLength() > 3) dragged_ = true;
        if (dragged_) panBy(at - *dragFrom_);
        dragFrom_ = at;
    }
    if (rulerStart || rubber != Rubber::None) overlay_->update();
}

void LayoutCanvas::mouseReleaseEvent(QMouseEvent* e) {
    dragFrom_.reset();
    if (swallowRelease_) {
        swallowRelease_ = false;
        return;
    }
    if (!dragged_) emit clicked(toWorld(e->position()), e->modifiers() & Qt::ShiftModifier);
}

void LayoutCanvas::mouseDoubleClickEvent(QMouseEvent*) {
    swallowRelease_ = true;  // the second release of a double click is not one more point
    emit doubleClicked();
}

void LayoutCanvas::refreshOverlay() { overlay_->update(); }

void LayoutCanvas::wheelEvent(QWheelEvent* e) {
    // A trackpad scrolls (its pinch arrives as a gesture); a mouse wheel, or Ctrl with either, zooms
    if (!e->pixelDelta().isNull() && !(e->modifiers() & Qt::ControlModifier)) panBy(e->pixelDelta());
    else zoomBy(std::pow(1.0015, e->angleDelta().y()), e->position());
    e->accept();
}

bool LayoutCanvas::event(QEvent* e) {
    if (e->type() == QEvent::NativeGesture) {
        auto* g = static_cast<QNativeGestureEvent*>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            zoomBy(1.0 + g->value(), g->position());
            return true;
        }
    }
    return QRhiWidget::event(e);
}

void LayoutCanvas::releaseResources() {
    lines_.reset();
    triangles_.reset();
    srb_.reset();
    lineSrb_.reset();
    ubuf_.reset();
    lineBuf_.reset();
    vbuf_.reset();
    rhi_ = nullptr;
    dirty_ = true;
}

void LayoutCanvas::initialize(QRhiCommandBuffer*) {
    if (rhi_ != rhi()) releaseResources();
    if (lines_) return;
    rhi_ = rhi();
    const auto makeUniforms = [&](std::unique_ptr<QRhiBuffer>& buf, std::unique_ptr<QRhiShaderResourceBindings>& srb) {
        buf.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniforms));
        buf->create();
        srb.reset(rhi_->newShaderResourceBindings());
        srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, buf.get())});
        srb->create();
    };
    makeUniforms(ubuf_, srb_);
    makeUniforms(lineBuf_, lineSrb_);
    QRhiVertexInputLayout layout;
    layout.setBindings({{kVertex}});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, {0, 1, QRhiVertexInputAttribute::UNormByte4, 2 * sizeof(float)}});
    auto make = [&](QRhiGraphicsPipeline::Topology t) {
        std::unique_ptr<QRhiGraphicsPipeline> p(rhi_->newGraphicsPipeline());
        p->setTopology(t);
        p->setShaderStages({{QRhiShaderStage::Vertex, shader(":/shaders/layout.vert.qsb")},
                            {QRhiShaderStage::Fragment, shader(":/shaders/layout.frag.qsb")}});
        QRhiGraphicsPipeline::TargetBlend blend;  // the fragment shader premultiplies
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        p->setTargetBlends({blend});
        p->setVertexInputLayout(layout);
        p->setShaderResourceBindings(srb_.get());
        p->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        p->setSampleCount(sampleCount());
        p->create();
        return p;
    };
    lines_ = make(QRhiGraphicsPipeline::Lines);
    triangles_ = make(QRhiGraphicsPipeline::Triangles);
    dirty_ = true;
}

void LayoutCanvas::render(QRhiCommandBuffer* cb) {
    QRhiResourceUpdateBatch* u = rhi_->nextResourceUpdateBatch();
    if (dirty_ && flat_ && styles_) {
        // ponytail: every vertex carries its layer's colour, 12 bytes each, and a colour or theme change re-uploads. A shape
        // is about 170 bytes on the GPU, so 1e7 shapes is 1.7 GB; levels of detail are the upgrade for chips of that size.
        quint64 vertices = 0;
        for (const auto& l : flat_->layers) vertices += (l.tri.size() + l.line.size()) / 2;
        QByteArray bytes((qsizetype)(vertices * kVertex), Qt::Uninitialized);
        char* out = bytes.data();
        fills_.assign(flat_->layers.size(), {});
        outlines_.assign(flat_->layers.size(), {});
        quint32 at = 0;
        // A layer that covers the chip (a well, a metal's fill) must not wash out the ones that draw the design:
        // its fill fades with its coverage, and the outlines keep it readable.
        std::vector<double> covered(flat_->layers.size(), 0.0);
        for (const auto& s : flat_->shapes) covered[s.layer] += (double)(s.x1 - s.x0) * (double)(s.y1 - s.y0);
        const double chip = flat_->box.empty() ? 1.0 : std::max(1.0, (flat_->box.x1 - flat_->box.x0) * (flat_->box.y1 - flat_->box.y0));
        const double strongest = Theme::current().dark ? 0.30 : 0.34;
        auto put = [&](const std::vector<float>& xy, const QColor& c, float alpha) {
            const unsigned char rgba[4] = {(unsigned char)c.red(), (unsigned char)c.green(), (unsigned char)c.blue(), (unsigned char)(alpha * 255)};
            for (size_t k = 0; k + 1 < xy.size(); k += 2) {
                memcpy(out, &xy[k], 8);
                memcpy(out + 8, rgba, 4);
                out += kVertex;
            }
        };
        for (size_t k = 0; k < flat_->layers.size(); ++k) {
            const QColor c = (*styles_)[k].color;
            fills_[k] = {at, (quint32)(flat_->layers[k].tri.size() / 2)};
            put(flat_->layers[k].tri, c, (float)std::max(0.05, strongest / (1.0 + 4.0 * covered[k] / chip)));
            at += fills_[k].count;
            outlines_[k] = {at, (quint32)(flat_->layers[k].line.size() / 2)};
            put(flat_->layers[k].line, c, 0.95f);
            at += outlines_[k].count;
        }
        if (!vbuf_ || (quint64)vbuf_->size() < (quint64)bytes.size()) {
            vbuf_.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, (quint32)std::max<qsizetype>(bytes.size(), 16)));
            vbuf_->create();
        }
        if (!bytes.isEmpty()) u->uploadStaticBuffer(vbuf_.get(), 0, (quint32)bytes.size(), bytes.constData());
        dirty_ = false;
    }
    const float w = width() / (float)scale_, h = height() / (float)scale_;
    QMatrix4x4 mvp = rhi_->clipSpaceCorrMatrix();
    mvp.ortho((float)cx_ - w / 2, (float)cx_ + w / 2, (float)cy_ - h / 2, (float)cy_ + h / 2, -1, 1);
    u->updateDynamicBuffer(ubuf_.get(), 0, 64, mvp.constData());
    u->updateDynamicBuffer(lineBuf_.get(), 0, 64, mvp.constData());
    // The mean spacing of shapes on screen: at about 8 pixels outlines are whole, and they fade below it
    float outline = 1.0f;
    if (flat_ && !flat_->shapes.empty() && !flat_->box.empty()) {
        const double area = (flat_->box.x1 - flat_->box.x0) * (flat_->box.y1 - flat_->box.y0);
        outline = (float)std::clamp(scale_ * std::sqrt(area / flat_->shapes.size()) / 8.0, 0.1, 1.0);
    }
    const float whole = 1.0f;
    u->updateDynamicBuffer(ubuf_.get(), 64, 4, &whole);
    u->updateDynamicBuffer(lineBuf_.get(), 64, 4, &outline);

    const QSize size = renderTarget()->pixelSize();
    cb->beginPass(renderTarget(), Theme::current().canvas, {1.0f, 0}, u);
    cb->setViewport({0, 0, (float)size.width(), (float)size.height()});
    if (vbuf_ && styles_) {
        const QRhiCommandBuffer::VertexInput in(vbuf_.get(), 0);
        const auto pass = [&](QRhiGraphicsPipeline* pipeline, QRhiShaderResourceBindings* srb, const std::vector<Range>& ranges) {
            cb->setGraphicsPipeline(pipeline);
            cb->setShaderResources(srb);
            cb->setVertexInput(0, 1, &in);
            for (size_t k = 0; k < ranges.size(); ++k)
                if ((*styles_)[k].visible && ranges[k].count) cb->draw(ranges[k].count, 1, ranges[k].first);
        };
        pass(triangles_.get(), srb_.get(), fills_);
        pass(lines_.get(), lineSrb_.get(), outlines_);
    }
    cb->endPass();
}

LayoutViewer::LayoutViewer(Session& session, const QString& lib, const QString& cell, const QString& view, QWidget* parent)
    : QMainWindow(parent, Qt::Window), session_(session), lib_(lib), cell_(cell), view_(view), canvas_(new LayoutCanvas(this)) {
    setWindowTitle("Layout: " + lib + " " + cell + " " + view);
    setCentralWidget(canvas_);

    auto* dock = new QDockWidget("Layers", this);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layerFilter_ = new QLineEdit(panel);
    layerFilter_->setPlaceholderText("Filter layers");
    layerFilter_->setClearButtonEnabled(true);
    layerTree_ = new QTreeWidget(panel);
    layerTree_->setHeaderHidden(true);
    layerTree_->setRootIsDecorated(false);
    layerTree_->setIconSize({14, 14});
    layout->addWidget(layerFilter_);
    layout->addWidget(layerTree_, 1);
    panel->setMinimumWidth(210);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    markerDock_ = new QDockWidget("Markers", this);
    markerDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    markerTree_ = new QTreeWidget(markerDock_);
    markerTree_->setHeaderHidden(true);
    markerTree_->setAccessibleName("What the check found");
    markerDock_->setWidget(markerTree_);
    addDockWidget(Qt::RightDockWidgetArea, markerDock_);
    markerDock_->hide();
    connect(markerTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* i) {
        if (i && i->data(0, Qt::UserRole).isValid()) showMarker(i->data(0, Qt::UserRole).toInt());
    });

    status_ = new QLabel(this);
    position_ = new QLabel(this);
    statusBar()->addWidget(status_, 1);
    statusBar()->addPermanentWidget(position_);

    connect(layerFilter_, &QLineEdit::textChanged, this, [this](const QString& q) {
        for (int k = 0; k < layerTree_->topLevelItemCount(); ++k) {
            auto* i = layerTree_->topLevelItem(k);
            i->setHidden(!q.isEmpty() && !i->text(0).contains(q, Qt::CaseInsensitive));
        }
    });
    connect(layerTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* i) {
        const layout::LayerKey key{i->data(0, Qt::UserRole).toPoint().x(), i->data(0, Qt::UserRole).toPoint().y()};
        shown_[key] = i->checkState(0) == Qt::Checked;
        for (size_t k = 0; k < flat_.layers.size(); ++k)
            if (flat_.layers[k].key == key) {
                styles_[k].visible = shown_[key];
                canvas_->visibilityChanged();
            }
    });
    connect(layerTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* i) {
        if (i) current_ = layout::LayerKey{i->data(0, Qt::UserRole).toPoint().x(), i->data(0, Qt::UserRole).toPoint().y()};
    });
    connect(&session_, &Session::changed, this, [this] { reloadIfChanged(); });
    connect(canvas_, &LayoutCanvas::doubleClicked, this, [this] { finishTool(); });
    connect(canvas_, &LayoutCanvas::moved, this, [this](QPointF w) {
        if (tool_ != Tool::Select) canvas_->cursor = worldOf(snapped(w));
        const double um = canvas_->micronsPerDbu;
        position_->setText(QString("%1, %2 µm").arg((w.x() + flat_.originX) * um, 0, 'f', 3).arg((w.y() + flat_.originY) * um, 0, 'f', 3));
    });
    connect(canvas_, &LayoutCanvas::clicked, this, [this](QPointF w, bool extend) { click(w, extend); });
    connect(static_cast<ThemeNotifier*>(Theme::notifier()), &ThemeNotifier::changed, this, [this] {
        buildStyles();
        fillLayers();
        canvas_->geometryChanged();
    });

    auto act = [this](QMenu* m, const QString& text, const QKeySequence& key, auto fn) {
        QAction* a = m->addAction(text, this, fn);
        a->setShortcut(key);
        return a;
    };
    QMenu* edit = menuBar()->addMenu("&Edit");
    act(edit, "Undo", QKeySequence("U"), [this] { undo(); });
    act(edit, "Redo", QKeySequence("Shift+U"), [this] { redo(); });
    edit->addSeparator();
    act(edit, "Move", QKeySequence("M"), [this] { startTool(Tool::Move); });
    act(edit, "Copy", QKeySequence("C"), [this] { startTool(Tool::Copy); });
    act(edit, "Stretch", QKeySequence("S"), [this] { startTool(Tool::Stretch); });
    act(edit, "Delete", QKeySequence::Delete, [this] { deleteSelection(); });
    act(edit, "Delete", QKeySequence(Qt::Key_Backspace), [this] { deleteSelection(); })->setVisible(false);
    act(edit, "Finish", QKeySequence(Qt::Key_Return), [this] { finishTool(); })->setVisible(false);
    act(edit, "Finish", QKeySequence(Qt::Key_Enter), [this] { finishTool(); })->setVisible(false);
    act(edit, "Cancel", QKeySequence(Qt::Key_Escape), [this] { cancelTool(); });
    QMenu* create = menuBar()->addMenu("&Create");
    act(create, "Rectangle", QKeySequence("R"), [this] { startTool(Tool::Rect); });
    act(create, "Path", QKeySequence("P"), [this] { startTool(Tool::Path); });
    act(create, "Polygon", QKeySequence("Shift+P"), [this] { startTool(Tool::Polygon); });
    act(create, "Via", QKeySequence("O"), [this] { startTool(Tool::Via); });
    act(create, "Label…", QKeySequence("L"), [this] { askLabel(Tool::Label); });
    act(create, "Pin…", QKeySequence("Ctrl+P"), [this] { askLabel(Tool::Pin); });
    create->addSeparator();
    act(create, "Instance…", QKeySequence("I"), [this] { askPlaceInstance(); });
    act(create, "Transistor…", QKeySequence("Shift+I"), [this] { askTransistor(); });
    QMenu* viewMenu = menuBar()->addMenu("&View");
    QAction* flight = viewMenu->addAction("Flight Lines", this, [this](bool on) { setFlightLinesShown(on); });
    flight->setCheckable(true);
    flight->setChecked(true);
    act(viewMenu, "Fit", QKeySequence("F"), [this] { canvas_->fit(); });
    act(viewMenu, "Show All Levels", QKeySequence("Shift+F"), [this] { setLevels(1 << 30); });
    act(viewMenu, "Show Top Level Only", QKeySequence("Ctrl+F"), [this] { setLevels(0); });
    act(viewMenu, "Zoom In", QKeySequence("]"), [this] { canvas_->zoomBy(1.5, canvas_->rect().center()); });
    act(viewMenu, "Zoom Out", QKeySequence("["), [this] { canvas_->zoomBy(1 / 1.5, canvas_->rect().center()); });
    QMenu* measure = menuBar()->addMenu("&Measure");
    act(measure, "Ruler", QKeySequence("K"), [this] { startRuler(); });
    act(measure, "Clear Rulers", QKeySequence("Shift+K"), [this] { clearRulers(); });
    QMenu* layers = menuBar()->addMenu("&Layers");
    auto setAll = [this](bool on) {
        layerTree_->blockSignals(true);
        for (int k = 0; k < layerTree_->topLevelItemCount(); ++k) layerTree_->topLevelItem(k)->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
        layerTree_->blockSignals(false);
        for (auto& s : styles_) s.visible = on;
        canvas_->visibilityChanged();
    };
    layers->addAction("Show All", this, [setAll] { setAll(true); });
    layers->addAction("Hide All", this, [setAll] { setAll(false); });

    reload();
}

LayoutViewer::~LayoutViewer() = default;

void LayoutViewer::buildStyles() {
    std::map<layout::LayerKey, QJsonObject> table;
    for (const auto& v : session_.call("layout_layers", {{"library", lib_}}).value.toArray()) {
        const QJsonObject o = v.toObject();
        table[{o["layer"].toInt(), o["datatype"].toInt()}] = o;
    }
    styles_.clear();
    for (size_t k = 0; k < flat_.layers.size(); ++k) {
        const auto& key = flat_.layers[k].key;
        LayerStyle s;
        const QString numbers = QString("%1/%2").arg(key.first).arg(key.second);
        const auto it = table.find(key);
        s.name = it == table.end() ? numbers : it->second["name"].toString() + "  " + numbers;
        s.color = it == table.end() ? spread(key) : QColor(it->second["color"].toString());
        s.visible = shown_.try_emplace(key, it == table.end() || it->second["visible"].toBool()).first->second;
        // A .lyp's white and black vanish on one theme's canvas: mid-grey carries both
        if (s.color.lightnessF() > 0.92 || s.color.lightnessF() < 0.08) s.color = Theme::current().muted;
        styles_.push_back(s);
    }
}

void LayoutViewer::fillLayers() {
    layerTree_->blockSignals(true);
    layerTree_->clear();
    const auto addRow = [&](const layout::LayerKey& key, const QString& name, const QColor& color, qulonglong shapes) {
        auto* item = new QTreeWidgetItem(layerTree_);
        item->setText(0, name);
        item->setToolTip(0, QString("%1 shapes").arg(shapes));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const auto seen = shown_.find(key);
        item->setCheckState(0, seen == shown_.end() || seen->second ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, QPoint(key.first, key.second));
        QPixmap chip(14, 14);
        chip.fill(color);
        item->setIcon(0, QIcon(chip));
        if (current_ && *current_ == key) layerTree_->setCurrentItem(item);
    };
    std::set<layout::LayerKey> listed;
    for (size_t k = 0; k < styles_.size(); ++k) {
        if (!flat_.layers[k].shapes) continue;
        listed.insert(flat_.layers[k].key);
        addRow(flat_.layers[k].key, styles_[k].name, styles_[k].color, flat_.layers[k].shapes);
    }
    // What the process offers to draw on and nothing uses yet
    for (const auto& value : session_.call("layout_layers", {{"library", lib_}}).value.toArray()) {
        const QJsonObject o = value.toObject();
        const layout::LayerKey key{o["layer"].toInt(), o["datatype"].toInt()};
        if (listed.insert(key).second) addRow(key, o["name"].toString() + QString("  %1/%2").arg(key.first).arg(key.second), QColor(o["color"].toString()), 0);
    }
    if (!current_ && layerTree_->topLevelItemCount()) {
        for (int k = 0; k < layerTree_->topLevelItemCount(); ++k)
            if (layerTree_->topLevelItem(k)->text(0).startsWith("Metal1")) {
                layerTree_->setCurrentItem(layerTree_->topLevelItem(k));
                current_ = layout::LayerKey{layerTree_->topLevelItem(k)->data(0, Qt::UserRole).toPoint().x(), layerTree_->topLevelItem(k)->data(0, Qt::UserRole).toPoint().y()};
                break;
            }
    }
    layerTree_->blockSignals(false);
}

void LayoutViewer::reload() {
    // ponytail: loads on the GUI thread (a 13 MB routed block takes about a second); a worker when designs outgrow that
    hierarchy_ = std::make_unique<layout::Hierarchy>(session_.workspace(), lib_.toStdString(), cell_.toStdString(), view_.toStdString());
    flat_ = hierarchy_->flatten(levels_);
    canvas_->micronsPerDbu = hierarchy_->metersPerDbu() * 1e6;
    const QJsonObject tech = session_.call("layout_tech_get", {{"library", lib_}}).value.toObject();
    grid_ = std::max<qint64>(1, (qint64)tech["grid"].toDouble(1));
    buildStyles();
    fillLayers();
    canvas_->setScene(&flat_, &styles_);
    refreshSelection();
    refreshFlightLines();
    placeMarkers();
    lastRevision_ = 0;
    for (const auto& name : session_.workspace().libraries()) lastRevision_ += session_.workspace().library(name).revision();
    const double um = canvas_->micronsPerDbu;
    if (flat_.box.empty()) status_->setText("Empty. Choose a layer and press R to draw.");
    else status_->setText(QString("%L1 shapes, %L2 instances, %3 x %4")
                         .arg(flat_.shapes.size()).arg(flat_.instances)
                         .arg(engineering((flat_.box.x1 - flat_.box.x0) * um), engineering((flat_.box.y1 - flat_.box.y0) * um)));
}

QString LayoutViewer::status() const { return status_->text(); }

bool LayoutViewer::setLayerVisible(int layer, int datatype, bool visible) {
    for (size_t k = 0; k < flat_.layers.size(); ++k) {
        if (flat_.layers[k].key != layout::LayerKey{layer, datatype}) continue;
        styles_[k].visible = visible;
        shown_[flat_.layers[k].key] = visible;
        fillLayers();
        canvas_->visibilityChanged();
        return true;
    }
    return false;
}

void LayoutViewer::setLevels(int levels) {
    if (levels == levels_) return;
    levels_ = levels;
    reload();
}

QJsonObject LayoutViewer::selectAt(QPointF w, bool extend) {
    // Linear in shapes: a click, not a frame. The smallest shape under the point wins, so a via beats the metal it sits on;
    // a label within a few pixels wins only over a shape much larger than that reach, so zoomed out the design stays selectable.
    if (!extend) selection_.clear();
    std::optional<size_t> best;
    double bestArea = 1e300;
    const float x = (float)w.x(), y = (float)w.y();
    for (size_t k = 0; k < flat_.shapes.size(); ++k) {
        const auto& s = flat_.shapes[k];
        if (x < s.x0 || x > s.x1 || y < s.y0 || y > s.y1 || !styles_[s.layer].visible) continue;
        const double area = (double)(s.x1 - s.x0) * (double)(s.y1 - s.y0);
        if (area >= bestArea) continue;
        const auto& tri = flat_.layers[s.layer].tri;
        bool inside = s.triCount == 0;
        for (uint32_t t = 0; !inside && t + 3 <= s.triCount; t += 3) {
            const float* v = &tri[2 * (size_t)(s.tri + t)];
            const auto side = [&](int a, int b) { return (v[2 * b] - v[2 * a]) * (y - v[2 * a + 1]) - (v[2 * b + 1] - v[2 * a + 1]) * (x - v[2 * a]); };
            const float d0 = side(0, 1), d1 = side(1, 2), d2 = side(2, 0);
            inside = (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
        }
        if (inside) { best = k; bestArea = area; }
    }
    const double um = canvas_->micronsPerDbu;
    const double reach = 6.0 / canvas_->scale();
    const FlatLabelPick label = nearestLabel(flat_, styles_, w, reach);
    if (label.label && (!best || std::min(flat_.shapes[*best].x1 - flat_.shapes[*best].x0, flat_.shapes[*best].y1 - flat_.shapes[*best].y0) > 4 * reach)) {
        const auto& l = *label.label;
        if (extend && selection_.count(l.object)) selection_.erase(l.object);
        else selection_.insert(l.object);
        refreshSelection();
        status_->setText(QString::fromStdString(l.text) + " on " + styles_[l.layer].name);
        const auto key = flat_.layers[l.layer].key;
        return {{"layer", key.first}, {"datatype", key.second}, {"text", QString::fromStdString(l.text)}, {"object", (double)l.object}};
    }
    if (!best) {
        refreshSelection();
        return {};
    }
    const auto& s = flat_.shapes[*best];
    if (extend && selection_.count(s.object)) selection_.erase(s.object);
    else selection_.insert(s.object);
    refreshSelection();
    const auto key = flat_.layers[s.layer].key;
    const QString master = QString::fromStdString(flat_.masters[s.master]);
    const double wx = (s.x1 - s.x0) * um, hy = (s.y1 - s.y0) * um;
    status_->setText(styles_[s.layer].name + "   in " + master + "   " + engineering(wx) + " x " + engineering(hy));
    return {{"layer", key.first}, {"datatype", key.second}, {"master", master}, {"width_um", wx}, {"height_um", hy},
            {"x0_um", (s.x0 + flat_.originX) * um}, {"y0_um", (s.y0 + flat_.originY) * um}, {"object", (double)s.object}};
}

void LayoutViewer::refreshSelection() {
    // One box per object: an instance's is the union of what it draws
    std::map<int64_t, QRectF> boxes;
    const auto note = [&](int64_t object, const QRectF& r) {
        if (!selection_.count(object)) return;
        auto [it, fresh] = boxes.try_emplace(object, r);
        if (!fresh) it->second = it->second.united(r);
    };
    if (!selection_.empty()) {
        for (const auto& s : flat_.shapes) note(s.object, QRectF(QPointF(s.x0, s.y0), QPointF(s.x1, s.y1)));
        const double reach = 3.0 / canvas_->scale();
        for (const auto& l : flat_.labels) note(l.object, QRectF(l.x - reach, l.y - reach, 2 * reach, 2 * reach));
    }
    std::set<int64_t> alive;
    canvas_->selectedBoxes.clear();
    for (const auto& [object, r] : boxes) {
        alive.insert(object);
        canvas_->selectedBoxes.push_back(r);
    }
    selection_ = alive;  // what an undo or another window removed
    canvas_->refreshOverlay();
}

void LayoutViewer::refreshFlightLines() {
    canvas_->flightLines.clear();
    if (!flightShown_) {
        canvas_->refreshOverlay();
        return;
    }
    const cmd::Result r = session_.call("layout_connectivity", {{"library", lib_}, {"cell", cell_}, {"view", view_}});
    if (r.ok)
        for (const auto& net : r.value.toObject()["nets"].toArray())
            for (const auto& line : net.toObject()["flightLines"].toArray()) {
                const QJsonArray a = line.toArray();
                canvas_->flightLines.push_back({{a[0].toDouble() - flat_.originX, a[1].toDouble() - flat_.originY},
                                                {a[2].toDouble() - flat_.originX, a[3].toDouble() - flat_.originY}});
            }
    canvas_->refreshOverlay();
}

void LayoutViewer::setFlightLinesShown(bool on) {
    flightShown_ = on;
    refreshFlightLines();
}

void LayoutViewer::placeMarkers() {
    canvas_->markers.clear();
    markerOfItem_.clear();
    const double dbuPerUm = 1.0 / canvas_->micronsPerDbu;
    for (const auto& rv : markerReport_["rules"].toArray())
        for (const auto& mv : rv.toObject()["markers"].toArray()) {
            const QJsonObject m = mv.toObject();
            const QJsonArray points = m["points"].toArray();
            if (points.isEmpty()) continue;
            const QString cell = m["cell"].toString();
            std::vector<layout::Xform> where = hierarchy_->placements(cell.toStdString());
            if (cell.isEmpty()) where = {layout::Xform{}};
            for (const auto& t : where) {
                layout::Box box;
                for (const auto& pv : points) {
                    double x, y;
                    t.apply(pv.toArray()[0].toDouble() * dbuPerUm, pv.toArray()[1].toDouble() * dbuPerUm, x, y);
                    box.add(x - flat_.originX, y - flat_.originY);
                }
                canvas_->markers.push_back(QRectF(QPointF(box.x0, box.y0), QPointF(box.x1, box.y1)));
            }
        }
    canvas_->refreshOverlay();
}

int LayoutViewer::setMarkers(const QJsonObject& report) {
    markerReport_ = report;
    placeMarkers();
    markerTree_->blockSignals(true);
    markerTree_->clear();
    int at = 0;
    const double dbuPerUm = 1.0 / canvas_->micronsPerDbu;
    for (const auto& rv : report["rules"].toArray()) {
        const QJsonObject rule = rv.toObject();
        auto* top = new QTreeWidgetItem(markerTree_, {rule["rule"].toString() + "  " + QString::number(rule["count"].toInt())});
        top->setToolTip(0, rule["description"].toString());
        for (const auto& mv : rule["markers"].toArray()) {
            const QJsonObject m = mv.toObject();
            const QString cell = m["cell"].toString();
            if (m["points"].toArray().isEmpty()) continue;
            const size_t places = cell.isEmpty() ? 1 : hierarchy_->placements(cell.toStdString()).size();
            for (size_t k = 0; k < places; ++k, ++at) {
                const QRectF r = canvas_->markers[at];
                auto* item = new QTreeWidgetItem(top, {QString("%1, %2 µm").arg((r.center().x() + flat_.originX) / dbuPerUm, 0, 'f', 2).arg((r.center().y() + flat_.originY) / dbuPerUm, 0, 'f', 2)});
                item->setData(0, Qt::UserRole, at);
                item->setToolTip(0, rule["description"].toString() + (cell.isEmpty() ? QString() : "\nin " + cell));
            }
        }
    }
    markerTree_->blockSignals(false);
    markerDock_->setVisible(!canvas_->markers.empty() || report["total"].toInt() > 0);
    canvas_->refreshOverlay();
    return (int)canvas_->markers.size();
}

QRectF LayoutViewer::markerWindow(int index) const {
    if (index < 0 || index >= (int)canvas_->markers.size()) return {};
    const QRectF r = canvas_->markers[index];
    const double margin = std::max({r.width(), r.height(), 400.0}) * 2;
    return r.adjusted(-margin, -margin, margin, margin);
}

void LayoutViewer::showMarker(int index) {
    if (index < 0 || index >= (int)canvas_->markers.size()) return;
    canvas_->currentMarker = index;
    const QRectF w = markerWindow(index);
    canvas_->zoomTo(w.left() + flat_.originX, w.top() + flat_.originY, w.right() + flat_.originX, w.bottom() + flat_.originY);
    canvas_->refreshOverlay();
}

void LayoutViewer::reloadIfChanged() {
    int64_t revision = 0;
    for (const auto& name : session_.workspace().libraries()) revision += session_.workspace().library(name).revision();
    if (revision != lastRevision_) reload();
}

// ---- Tools

oadb::Point LayoutViewer::snapped(QPointF w) const {
    const auto on = [&](double absolute) { return (int64_t)std::llround(absolute / (double)grid_) * grid_; };
    return {on(w.x() + flat_.originX), on(w.y() + flat_.originY)};
}

QPointF LayoutViewer::worldOf(oadb::Point p) const { return {(double)p.x - flat_.originX, (double)p.y - flat_.originY}; }

void LayoutViewer::hint(const QString& text) { status_->setText(text); }

QString LayoutViewer::currentLayerName() const {
    return current_ ? QString("%1/%2").arg(current_->first).arg(current_->second) : QString();
}

bool LayoutViewer::currentIsMetal(QString* name) const {
    if (!current_) return false;
    const QJsonObject tech = session_.call("layout_tech_get", {{"library", lib_}}).value.toObject();
    for (const auto& l : tech["layers"].toArray())
        if (l.toObject()["layer"].toInt() == current_->first && l.toObject()["datatype"].toInt() == current_->second)
            for (const auto& m : tech["metals"].toArray())
                if (m.toObject()["name"].toString() == l.toObject()["name"].toString()) {
                    *name = m.toObject()["name"].toString();
                    return true;
                }
    return false;
}

bool LayoutViewer::run(const QString& command, QJsonObject args, QJsonValue* value) {
    args["library"] = lib_;
    args["cell"] = cell_;
    args["view"] = view_;
    const cmd::Result r = session_.call(command, args);
    if (!r.ok) {
        lastError_ = r.error;
        hint(r.error);
        return false;
    }
    lastError_.clear();
    if (value) *value = r.value;
    return true;
}

void LayoutViewer::setCurrentLayer(int layer, int datatype) {
    current_ = layout::LayerKey{layer, datatype};
    for (int k = 0; k < layerTree_->topLevelItemCount(); ++k)
        if (layerTree_->topLevelItem(k)->data(0, Qt::UserRole).toPoint() == QPoint(layer, datatype)) layerTree_->setCurrentItem(layerTree_->topLevelItem(k));
}

void LayoutViewer::startTool(Tool t) {
    QString metal;
    if ((t == Tool::Move || t == Tool::Copy || t == Tool::Stretch) && selection_.empty()) {
        tool_ = Tool::Select;
        return hint("Select something first");
    }
    if (t == Tool::Via && !currentIsMetal(&metal)) {
        tool_ = Tool::Select;
        return hint("Choose a metal layer for the via's lower side");
    }
    points_.clear();
    canvas_->rubber = LayoutCanvas::Rubber::None;
    canvas_->rubberPoints.clear();
    canvas_->ghost.clear();
    tool_ = t;
    switch (t) {
        case Tool::Rect: hint("Rectangle: click two corners. Esc ends."); break;
        case Tool::Path: hint("Path: click its points, Enter ends it."); break;
        case Tool::Polygon: hint("Polygon: click its points, Enter closes it."); break;
        case Tool::Via: hint("Via: click where its cuts go."); break;
        case Tool::Label: hint("Label: click where it goes."); break;
        case Tool::Pin: hint("Pin: click two corners."); break;
        case Tool::Instance: hint("Instance: click where " + instanceMaster_ + " goes."); break;
        case Tool::Move: hint("Move: click a reference point, then its destination."); break;
        case Tool::Copy: hint("Copy: click a reference point, then its destination."); break;
        case Tool::Stretch: hint("Stretch: click an edge of the selection, then where it goes."); break;
        default: break;
    }
    canvas_->refreshOverlay();
}

void LayoutViewer::cancelTool() {
    rulerMode_ = false;
    canvas_->rulerStart.reset();
    const bool wasIdle = tool_ == Tool::Select && points_.empty();
    tool_ = Tool::Select;
    points_.clear();
    canvas_->rubber = LayoutCanvas::Rubber::None;
    canvas_->rubberPoints.clear();
    canvas_->ghost.clear();
    if (wasIdle) {
        selection_.clear();
        refreshSelection();
    }
    canvas_->refreshOverlay();
}

void LayoutViewer::click(QPointF w, bool extend) {
    if (rulerMode_) {
        const QPointF at = worldOf(snapped(w));
        if (!canvas_->rulerStart) {
            canvas_->rulerStart = at;
        } else {
            canvas_->rulers.push_back({*canvas_->rulerStart, at});
            canvas_->rulerStart.reset();
            rulerMode_ = false;
        }
        canvas_->visibilityChanged();
        return;
    }
    if (tool_ == Tool::Select) selectAt(w, extend);
    else toolPoint(snapped(w));
}

void LayoutViewer::clickDbu(double x, double y, bool extend) { click({x - flat_.originX, y - flat_.originY}, extend); }

void LayoutViewer::toolPoint(oadb::Point at) {
    const auto pointJson = [](oadb::Point p) { return QJsonArray{(double)p.x, (double)p.y}; };
    const auto rubberFrom = [&](LayoutCanvas::Rubber kind) {
        canvas_->rubber = kind;
        canvas_->rubberPoints.clear();
        for (const auto& p : points_) canvas_->rubberPoints.push_back(worldOf(p));
        canvas_->cursor = worldOf(at);
        canvas_->refreshOverlay();
    };
    const auto resetRubber = [&] {
        points_.clear();
        canvas_->rubber = LayoutCanvas::Rubber::None;
        canvas_->rubberPoints.clear();
        canvas_->ghost.clear();
        canvas_->refreshOverlay();
    };
    switch (tool_) {
        case Tool::Rect:
        case Tool::Pin: {
            points_.push_back(at);
            if (points_.size() == 1) return rubberFrom(LayoutCanvas::Rubber::Box);
            const oadb::Point a = points_[0];
            resetRubber();
            if (tool_ == Tool::Rect) {
                run("layout_shape_add", {{"layer", currentLayerName()}, {"kind", "rect"}, {"points", QJsonArray{pointJson(a), pointJson(at)}}});
            } else {
                QString metal;
                if (!currentIsMetal(&metal)) return hint("A pin goes on a metal: choose one in the layer panel");
                run("layout_pin_add", {{"name", toolText_}, {"metal", metal},
                                       {"box", QJsonArray{(double)std::min(a.x, at.x), (double)std::min(a.y, at.y), (double)std::max(a.x, at.x), (double)std::max(a.y, at.y)}}});
            }
            return;
        }
        case Tool::Path:
        case Tool::Polygon:
            if (points_.empty() || !(points_.back() == at)) points_.push_back(at);
            return rubberFrom(tool_ == Tool::Path ? LayoutCanvas::Rubber::Line : LayoutCanvas::Rubber::Closed);
        case Tool::Via: {
            QString metal;
            if (currentIsMetal(&metal)) run("layout_via_add", {{"from", metal}, {"at", pointJson(at)}});
            return;
        }
        case Tool::Label:
            run("layout_shape_add", {{"layer", currentLayerName()}, {"kind", "text"}, {"points", QJsonArray{pointJson(at)}}, {"text", toolText_}});
            return;
        case Tool::Instance:
            run("layout_instance_add", {{"master", instanceMaster_}, {"at", pointJson(at)}});
            return;
        case Tool::Move:
        case Tool::Copy: {
            points_.push_back(at);
            if (points_.size() == 1) {
                canvas_->ghost = canvas_->selectedBoxes;
                return rubberFrom(LayoutCanvas::Rubber::Ghost);
            }
            const oadb::Point a = points_[0];
            const bool move = tool_ == Tool::Move;
            resetRubber();
            QJsonArray shapes, instances;
            for (int64_t o : selection_) (o > 0 ? shapes : instances).append((double)std::abs(o));
            if (run(move ? "layout_objects_move" : "layout_objects_copy", {{"shapes", shapes}, {"instances", instances}, {"dx", (double)(at.x - a.x)}, {"dy", (double)(at.y - a.y)}}))
                tool_ = Tool::Select;
            return;
        }
        case Tool::Stretch: {
            if (points_.empty()) {
                // The nearest edge of the selected shapes within a few pixels: its end points are what move
                const double reach = 8.0 / canvas_->scale();
                std::optional<std::pair<oadb::Point, oadb::Point>> edge;
                double bestDistance = reach;
                stretchShapes_.clear();
                for (const auto& shape : session_.workspace().library(lib_.toStdString()).layoutShapes(cell_.toStdString(), view_.toStdString())) {
                    if (!selection_.count(shape.id) || shape.kind != "polygon") continue;
                    for (size_t k = 0; k + 1 < shape.points.size(); ++k) {
                        const oadb::Point p = shape.points[k], q = shape.points[k + 1];
                        const double dx = (double)(q.x - p.x), dy = (double)(q.y - p.y), len2 = std::max(1.0, dx * dx + dy * dy);
                        const double u = std::clamp(((at.x - p.x) * dx + (at.y - p.y) * dy) / len2, 0.0, 1.0);
                        const double dist = std::hypot(p.x + u * dx - at.x, p.y + u * dy - at.y);
                        if (dist <= bestDistance) {
                            bestDistance = dist;
                            edge = {p, q};
                            stretchShapes_ = {shape.id};
                        }
                    }
                }
                if (!edge) return hint("Stretch: click on an edge of a selected polygon");
                stretchWindow_ = {std::min(edge->first.x, edge->second.x), std::min(edge->first.y, edge->second.y), std::max(edge->first.x, edge->second.x), std::max(edge->first.y, edge->second.y)};
                stretchX_ = edge->first.x == edge->second.x;   // an upright edge moves sideways
                stretchY_ = edge->first.y == edge->second.y;   // and a level one up and down
                points_.push_back(at);
                return rubberFrom(LayoutCanvas::Rubber::Line);
            }
            const oadb::Point a = points_[0];
            resetRubber();
            QJsonArray ids;
            for (int64_t id : stretchShapes_) ids.append((double)id);
            const bool free = !stretchX_ && !stretchY_;
            if (run("layout_stretch", {{"shapes", ids}, {"window", QJsonArray{(double)stretchWindow_[0], (double)stretchWindow_[1], (double)stretchWindow_[2], (double)stretchWindow_[3]}},
                                       {"dx", stretchX_ || free ? (double)(at.x - a.x) : 0.0}, {"dy", stretchY_ || free ? (double)(at.y - a.y) : 0.0}}))
                tool_ = Tool::Select;
            return;
        }
        default:
            return;
    }
}

void LayoutViewer::finishTool() {
    if (tool_ != Tool::Path && tool_ != Tool::Polygon) return;
    QJsonArray points;
    for (const auto& p : points_) points.append(QJsonArray{(double)p.x, (double)p.y});
    const bool isPath = tool_ == Tool::Path;
    const size_t have = points_.size();
    points_.clear();
    canvas_->rubber = LayoutCanvas::Rubber::None;
    canvas_->rubberPoints.clear();
    canvas_->refreshOverlay();
    if (have < (isPath ? 2u : 3u)) return hint(isPath ? "A path needs two points" : "A polygon needs three points");
    QJsonObject args{{"layer", currentLayerName()}, {"kind", isPath ? "path" : "polygon"}, {"points", points}};
    if (isPath) {
        QString metal;
        double width = 200;
        if (currentIsMetal(&metal))
            for (const auto& m : session_.call("layout_tech_get", {{"library", lib_}}).value.toObject()["metals"].toArray())
                if (m.toObject()["name"].toString() == metal) width = m.toObject()["minWidth"].toDouble();
        args["width"] = width;
        args["pathType"] = 2;
    }
    run("layout_shape_add", args);
}

void LayoutViewer::deleteSelection() {
    if (selection_.empty()) return hint("Select something first");
    QJsonArray shapes, instances;
    for (int64_t o : selection_) (o > 0 ? shapes : instances).append((double)std::abs(o));
    run("layout_objects_delete", {{"shapes", shapes}, {"instances", instances}});
}

void LayoutViewer::undo() {
    const cmd::Result r = session_.call("edit_undo", {{"library", lib_}});
    hint(r.ok ? "Undid " + r.value.toString() : r.error);
}

void LayoutViewer::redo() {
    const cmd::Result r = session_.call("edit_redo", {{"library", lib_}});
    hint(r.ok ? "Redid " + r.value.toString() : r.error);
}

void LayoutViewer::askLabel(Tool t) {
    bool ok = false;
    const QString text = QInputDialog::getText(this, t == Tool::Pin ? "Pin" : "Label", t == Tool::Pin ? "Net name" : "Text", QLineEdit::Normal, toolText_, &ok);
    if (!ok || text.isEmpty()) return;
    toolText_ = text;
    startTool(t);
}

void LayoutViewer::askPlaceInstance() {
    QStringList masters;
    for (const auto& c : session_.workspace().library(lib_.toStdString()).cells())
        for (const auto& w : session_.workspace().library(lib_.toStdString()).views(c))
            if (w.type == "layout" && QString::fromStdString(c) != cell_) masters << QString::fromStdString(c);
    if (masters.isEmpty()) return hint("No other layout in this library to place");
    bool ok = false;
    const QString cell = QInputDialog::getItem(this, "Place an instance", "Master", masters, 0, false, &ok);
    if (!ok) return;
    instanceMaster_ = cell;
    startTool(Tool::Instance);
}

void LayoutViewer::askTransistor() {
    const cmd::Command* c = session_.registry().find("layout_pcell_mos");
    if (!c) return;
    CommandForm form(*c, {{"library", lib_}, {"type", "nmos"}, {"w", 1000}, {"l", 130}, {"ng", 1}}, this);
    if (form.exec() != QDialog::Accepted) return;
    QJsonObject args;
    QString error;
    if (!form.arguments(&args, &error)) return hint(error);
    const cmd::Result r = session_.call("layout_pcell_mos", args);
    if (!r.ok) return hint(r.error);
    instanceMaster_ = r.value.toString();
    startTool(Tool::Instance);
}

QJsonObject LayoutViewer::selectAtDbu(double x, double y) { return selectAt({x - flat_.originX, y - flat_.originY}); }

void LayoutViewer::zoomToMicrons(double x0, double y0, double x1, double y1) {
    const double um = canvas_->micronsPerDbu;
    canvas_->zoomTo(x0 / um, y0 / um, x1 / um, y1 / um);
}

void LayoutViewer::startRuler() {
    cancelTool();
    rulerMode_ = true;
    canvas_->rulerStart.reset();
    status_->setText("Click the ruler's two ends");
}

void LayoutViewer::clearRulers() {
    canvas_->rulers.clear();
    canvas_->rulerStart.reset();
    rulerMode_ = false;
    canvas_->visibilityChanged();
}

}  // namespace app
