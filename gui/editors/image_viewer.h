#pragma once

#include "gui/core/editor_manager.h"
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QLabel>
#include <QVBoxLayout>

namespace acutesim::gui::editors {

/**
 * @brief Read-only image viewer with scroll-wheel zoom and drag-to-pan.
 *
 * Supports: PNG, JPG/JPEG, BMP, GIF, SVG (via QSvgRenderer if available).
 * Status bar shows image dimensions and current zoom %.
 */
class ImageViewer : public core::IEditor {
    Q_OBJECT
public:
    explicit ImageViewer(const QString& filePath, QWidget* parent = nullptr);

    QString filePath() const override { return filePath_; }
    QString title() const override;
    bool isDirty() const override { return false; }
    core::EditorCapabilities capabilities() const override { return {}; }

protected:
    void wheelEvent(QWheelEvent* event) override;

private:
    void loadImage();
    void updateStatus();

    QString filePath_;
    QGraphicsView* view_ = nullptr;
    QGraphicsScene* scene_ = nullptr;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    qreal zoomFactor_ = 1.0;
    QSize imageSize_;
};

} // namespace acutesim::gui::editors
