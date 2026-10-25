#include "app/canvas.h"

#include <QFile>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QWheelEvent>
#include <rhi/qrhi.h>

#include <cmath>

namespace app {

namespace {

constexpr float kBackground[3] = {0.05f, 0.05f, 0.07f};

QVector3D color(Role r) {
    switch (r) {
        case Role::Grid: return {0.32f, 0.32f, 0.36f};
        case Role::Device: return {0.35f, 0.85f, 0.35f};
        case Role::Pin: return {0.95f, 0.30f, 0.30f};
        case Role::Wire: return {0.30f, 0.60f, 1.00f};
        case Role::Label: return {1.00f, 0.85f, 0.35f};
        case Role::Annotate: return {0.95f, 0.60f, 0.25f};
        case Role::Marker: return {1.00f, 0.25f, 0.85f};
        case Role::Selected: return {1.00f, 1.00f, 1.00f};
        case Role::Preview: return {1.00f, 1.00f, 0.45f};
    }
    return {1, 1, 1};
}

QShader shader(const QString& name) {
    QFile f(name);
    if (!f.open(QIODevice::ReadOnly)) qFatal("canvas: missing shader %s", qPrintable(name));
    return QShader::fromSerialized(f.readAll());
}

}  // namespace

// The texts, painted over the RHI image in the canvas's coordinates
class TextLayer : public QWidget {
public:
    explicit TextLayer(Canvas* c) : QWidget(c), canvas_(c) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const double px = kTextHeight / canvas_->scale();
        if (px < 5) return;  // too small to read: the geometry alone
        QPainter p(this);
        QFont font("Menlo");
        font.setPixelSize((int)px);
        p.setFont(font);
        for (const auto& t : canvas_->display().texts) {
            const QPointF at = canvas_->toScreen(t.at);
            const QVector3D c = color(t.role);
            p.setPen(QColor::fromRgbF(c.x(), c.y(), c.z()));
            // a large box anchored at the point on the side the justification names
            constexpr double W = 4000, H = 400;
            const bool left = t.justify.endsWith("Left"), right = t.justify.endsWith("Right");
            const bool lower = t.justify.startsWith("lower"), upper = t.justify.startsWith("upper");
            const QRectF box(left ? at.x() : right ? at.x() - W : at.x() - W / 2, lower ? at.y() - H : upper ? at.y() : at.y() - H / 2, W, H);
            const int flags = (left ? Qt::AlignLeft : right ? Qt::AlignRight : Qt::AlignHCenter) |
                              (lower ? Qt::AlignBottom : upper ? Qt::AlignTop : Qt::AlignVCenter);
            p.drawText(box, flags, t.text);
        }
    }

private:
    Canvas* canvas_;
};

Canvas::Canvas(QWidget* parent) : QRhiWidget(parent), text_(new TextLayer(this)) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 150);
}

Canvas::~Canvas() = default;

void Canvas::changed() {
    update();
    text_->update();
}

void Canvas::setDisplay(Display d) {
    display_ = std::move(d);
    changed();
}

QPointF Canvas::toWorld(QPointF px) const {
    return {center_.x() + (px.x() - width() / 2.0) * scale_, center_.y() - (px.y() - height() / 2.0) * scale_};
}

QPointF Canvas::toScreen(QPointF w) const {
    return {width() / 2.0 + (w.x() - center_.x()) / scale_, height() / 2.0 - (w.y() - center_.y()) / scale_};
}

void Canvas::fit(const QRectF& world) {
    const QRectF r = world.isNull() ? QRectF(-1000, -1000, 2000, 2000) : world.normalized();
    center_ = r.center();
    scale_ = std::max({r.width() / std::max(1, width()), r.height() / std::max(1, height()), 0.01}) * 1.15;
    changed();
}

void Canvas::zoomBy(double factor, QPointF aboutPx) {
    const QPointF before = toWorld(aboutPx);
    scale_ = std::clamp(scale_ / factor, 0.01, 1e5);
    center_ += before - toWorld(aboutPx);  // keep the point under the cursor
    changed();
}

void Canvas::panBy(QPointF px) {
    center_ += QPointF(-px.x() * scale_, px.y() * scale_);
    changed();
}

void Canvas::resizeEvent(QResizeEvent* e) {
    QRhiWidget::resizeEvent(e);
    text_->setGeometry(rect());
}

void Canvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        last_ = e->position();
    } else if (e->button() == Qt::LeftButton) {
        emit pressed(toWorld(e->position()), e->modifiers());
    }
}

void Canvas::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        panBy(e->position() - last_);
        last_ = e->position();
    }
    emit moved(toWorld(e->position()));
}

void Canvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) panning_ = false;
    else if (e->button() == Qt::LeftButton) emit released(toWorld(e->position()));
}

void Canvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit doubleClicked(toWorld(e->position()));
}

void Canvas::wheelEvent(QWheelEvent* e) {
    const bool zoom = e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier);
    if (!zoom && !e->pixelDelta().isNull()) panBy(e->pixelDelta());  // a trackpad's two-finger scroll
    else zoomBy(std::pow(1.0015, e->angleDelta().y()), e->position());
    e->accept();
}

bool Canvas::event(QEvent* e) {
    if (e->type() == QEvent::NativeGesture) {
        auto* g = static_cast<QNativeGestureEvent*>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            zoomBy(1 + g->value(), g->position());
            return true;
        }
    }
    return QRhiWidget::event(e);
}

void Canvas::releaseResources() {
    lines_.reset();
    triangles_.reset();
    srb_.reset();
    ubuf_.reset();
    vbuf_.reset();
    rhi_ = nullptr;
}

void Canvas::initialize(QRhiCommandBuffer*) {
    if (rhi_ != rhi()) releaseResources();
    if (lines_) return;
    rhi_ = rhi();
    ubuf_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
    ubuf_->create();
    vbuf_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, 1 << 16));
    vbuf_->create();
    srb_.reset(rhi_->newShaderResourceBindings());
    srb_->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, ubuf_.get())});
    srb_->create();
    QRhiVertexInputLayout layout;
    layout.setBindings({{5 * sizeof(float)}});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, {0, 1, QRhiVertexInputAttribute::Float3, 2 * sizeof(float)}});
    auto make = [&](QRhiGraphicsPipeline::Topology t) {
        std::unique_ptr<QRhiGraphicsPipeline> p(rhi_->newGraphicsPipeline());
        p->setTopology(t);
        p->setShaderStages({{QRhiShaderStage::Vertex, shader(":/shaders/canvas.vert.qsb")},
                            {QRhiShaderStage::Fragment, shader(":/shaders/canvas.frag.qsb")}});
        p->setVertexInputLayout(layout);
        p->setShaderResourceBindings(srb_.get());
        p->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        p->setSampleCount(sampleCount());
        p->create();
        return p;
    };
    lines_ = make(QRhiGraphicsPipeline::Lines);
    triangles_ = make(QRhiGraphicsPipeline::Triangles);
}

void Canvas::render(QRhiCommandBuffer* cb) {
    // the frame's vertices: line segments (the grid's dots as one-pixel segments), then the squares' triangles
    std::vector<float> v;
    auto vertex = [&](QPointF p, const QVector3D& c) { v.insert(v.end(), {(float)p.x(), (float)p.y(), c.x(), c.y(), c.z()}); };
    const QPointF lo = toWorld(QPointF(0, height())), hi = toWorld(QPointF(width(), 0));
    double step = grid_;
    while (step / scale_ < 8) step *= 4;  // never denser than one dot per 8 pixels
    const double dot = scale_ / devicePixelRatioF();
    const QVector3D gc = color(Role::Grid);
    for (double x = std::ceil(lo.x() / step) * step; x <= hi.x(); x += step)
        for (double y = std::ceil(lo.y() / step) * step; y <= hi.y(); y += step) vertex({x, y}, gc), vertex({x + dot, y}, gc);
    for (const auto& s : display_.segs) vertex(s.a, color(s.role)), vertex(s.b, color(s.role));
    const quint32 lineVertices = (quint32)(v.size() / 5);
    for (const auto& d : display_.dots) {
        const QVector3D c = color(d.role);
        const double h = d.half;
        const QPointF a(d.at.x() - h, d.at.y() - h), b(d.at.x() + h, d.at.y() - h), e(d.at.x() + h, d.at.y() + h), f(d.at.x() - h, d.at.y() + h);
        for (QPointF p : {a, b, e, a, e, f}) vertex(p, c);
    }
    const quint32 triangleVertices = (quint32)(v.size() / 5) - lineVertices;

    const quint32 bytes = (quint32)(v.size() * sizeof(float));
    if (bytes > vbuf_->size()) {
        vbuf_->setSize(bytes * 2);
        vbuf_->create();
    }
    QMatrix4x4 mvp = rhi_->clipSpaceCorrMatrix();
    mvp.ortho(lo.x(), hi.x(), lo.y(), hi.y(), -1, 1);
    QRhiResourceUpdateBatch* u = rhi_->nextResourceUpdateBatch();
    if (bytes) u->updateDynamicBuffer(vbuf_.get(), 0, bytes, v.data());
    u->updateDynamicBuffer(ubuf_.get(), 0, 64, mvp.constData());

    const QSize size = renderTarget()->pixelSize();
    cb->beginPass(renderTarget(), QColor::fromRgbF(kBackground[0], kBackground[1], kBackground[2]), {1.0f, 0}, u);
    cb->setViewport({0, 0, (float)size.width(), (float)size.height()});
    auto draw = [&](QRhiGraphicsPipeline* p, quint32 first, quint32 count) {
        if (!count) return;
        cb->setGraphicsPipeline(p);
        cb->setShaderResources();
        const QRhiCommandBuffer::VertexInput in(vbuf_.get(), first * 5 * sizeof(float));
        cb->setVertexInput(0, 1, &in);
        cb->draw(count);
    };
    draw(lines_.get(), 0, lineVertices);
    draw(triangles_.get(), lineVertices, triangleVertices);
    cb->endPass();
}

}  // namespace app
