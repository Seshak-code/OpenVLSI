#include "image_viewer.h"
#include <QPixmap>
#include <QFileInfo>
#include <QWheelEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <QScrollBar>

namespace acutesim::gui::editors {

ImageViewer::ImageViewer(const QString& filePath, QWidget* parent)
    : core::IEditor(parent), filePath_(filePath)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    scene_ = new QGraphicsScene(this);
    view_ = new QGraphicsView(scene_, this);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setRenderHint(QPainter::SmoothPixmapTransform);
    view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view_->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view_->setFrameShape(QFrame::NoFrame);

    statusLabel_ = new QLabel(this);
    statusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    statusLabel_->setContentsMargins(6, 2, 6, 2);
    statusLabel_->setFixedHeight(22);

    layout->addWidget(view_);
    layout->addWidget(statusLabel_);

    loadImage();
}

QString ImageViewer::title() const {
    return QFileInfo(filePath_).fileName();
}

void ImageViewer::loadImage() {
    QPixmap pix(filePath_);
    if (pix.isNull()) {
        statusLabel_->setText("Cannot load image: " + filePath_);
        return;
    }

    imageSize_ = pix.size();
    pixmapItem_ = scene_->addPixmap(pix);
    scene_->setSceneRect(pix.rect());
    view_->fitInView(pixmapItem_, Qt::KeepAspectRatio);

    // Compute actual zoom after fitInView
    zoomFactor_ = view_->transform().m11();
    updateStatus();
}

void ImageViewer::wheelEvent(QWheelEvent* event) {
    const qreal scaleFactor = 1.15;
    if (event->angleDelta().y() > 0) {
        view_->scale(scaleFactor, scaleFactor);
        zoomFactor_ *= scaleFactor;
    } else {
        view_->scale(1.0 / scaleFactor, 1.0 / scaleFactor);
        zoomFactor_ /= scaleFactor;
    }
    updateStatus();
    event->accept();
}

void ImageViewer::updateStatus() {
    statusLabel_->setText(QString("%1 × %2 px  |  Zoom: %3%")
        .arg(imageSize_.width())
        .arg(imageSize_.height())
        .arg(qRound(zoomFactor_ * 100)));
}

} // namespace acutesim::gui::editors
