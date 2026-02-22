#pragma once

#include "gui/core/editor_manager.h"
#include <QAbstractScrollArea>
#include <QByteArray>
#include <QLabel>
#include <QVBoxLayout>

namespace acutesim::gui::editors {

/**
 * @brief Virtualized hex dump viewer for large binary files.
 *
 * Layout: offset | 16 hex bytes | ASCII representation
 * Uses QAbstractScrollArea for memory-efficient rendering of files of any size.
 */
class HexView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit HexView(QWidget* parent = nullptr);

    void setData(const QByteArray& data);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateScrollBar();
    int bytesPerLine() const { return 16; }
    int lineHeight() const;
    int offsetWidth() const;
    int hexWidth() const;
    int asciiStart() const;

    QByteArray data_;
    int charWidth_ = 0;
    int charHeight_ = 0;
};

class HexViewer : public core::IEditor {
    Q_OBJECT
public:
    explicit HexViewer(const QString& filePath, QWidget* parent = nullptr);

    QString filePath() const override { return filePath_; }
    QString title() const override;
    bool isDirty() const override { return false; }
    core::EditorCapabilities capabilities() const override { return {}; }

private:
    QString filePath_;
    HexView* hexView_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

} // namespace acutesim::gui::editors
