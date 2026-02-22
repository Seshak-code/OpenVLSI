#include "hex_viewer.h"
#include <QPainter>
#include <QScrollBar>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QResizeEvent>
#include "gui/styling/theme_manager.h"

namespace acutesim::gui::editors {

// ─── HexView ─────────────────────────────────────────────────────────────────

HexView::HexView(QWidget* parent) : QAbstractScrollArea(parent) {
    QFont mono("Courier New", 10);
    mono.setStyleHint(QFont::Monospace);
    setFont(mono);
    QFontMetrics fm(mono);
    charWidth_  = fm.horizontalAdvance('X');
    charHeight_ = fm.height();
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void HexView::setData(const QByteArray& data) {
    data_ = data;
    updateScrollBar();
    viewport()->update();
}

int HexView::lineHeight() const { return charHeight_ + 2; }

int HexView::offsetWidth() const {
    // "0x00000000  " = 12 chars
    return charWidth_ * 12;
}

int HexView::hexWidth() const {
    // 16 bytes * "XX " = 48 chars + one extra space = 49
    return charWidth_ * 49;
}

int HexView::asciiStart() const {
    return offsetWidth() + hexWidth();
}

void HexView::updateScrollBar() {
    int totalLines = (data_.size() + bytesPerLine() - 1) / bytesPerLine();
    int visibleLines = viewport()->height() / lineHeight();
    verticalScrollBar()->setRange(0, qMax(0, totalLines - visibleLines));
    verticalScrollBar()->setPageStep(visibleLines);
    verticalScrollBar()->setSingleStep(1);
}

void HexView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBar();
}

void HexView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    auto& tm = acutesim::gui::styling::ThemeManager::instance();

    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), tm.getColor("editor.background"));
    painter.setFont(font());

    QFontMetrics fm(font());
    const int firstLine  = verticalScrollBar()->value();
    const int visLines   = viewport()->height() / lineHeight() + 1;
    const int bpl        = bytesPerLine();
    const QColor colOffset(tm.getColor("ui.textDim"));
    const QColor colHex(tm.getColor("editor.foreground"));
    const QColor colAscii(tm.getColor("ui.accent"));

    for (int row = 0; row < visLines; ++row) {
        int lineIdx = firstLine + row;
        int byteOff = lineIdx * bpl;
        if (byteOff >= data_.size()) break;

        int y = row * lineHeight() + fm.ascent() + 1;

        // Offset
        painter.setPen(colOffset);
        painter.drawText(2, y,
            QString("0x%1  ").arg(byteOff, 8, 16, QChar('0')));

        // Hex bytes
        painter.setPen(colHex);
        for (int col = 0; col < bpl; ++col) {
            int idx = byteOff + col;
            QString hex = (idx < data_.size())
                ? QString("%1 ").arg((unsigned char)data_[idx], 2, 16, QChar('0'))
                : "   ";
            int x = offsetWidth() + col * charWidth_ * 3 + 2;
            painter.drawText(x, y, hex);
        }

        // ASCII
        painter.setPen(colAscii);
        for (int col = 0; col < bpl; ++col) {
            int idx = byteOff + col;
            if (idx >= data_.size()) break;
            char c = data_[idx];
            QString ch = (c >= 0x20 && c < 0x7f) ? QString(c) : ".";
            int x = asciiStart() + col * charWidth_ + 2;
            painter.drawText(x, y, ch);
        }
    }
}

// ─── HexViewer ───────────────────────────────────────────────────────────────

HexViewer::HexViewer(const QString& filePath, QWidget* parent)
    : core::IEditor(parent), filePath_(filePath)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    hexView_ = new HexView(this);
    statusLabel_ = new QLabel(this);
    statusLabel_->setContentsMargins(6, 2, 6, 2);
    statusLabel_->setFixedHeight(22);

    layout->addWidget(hexView_);
    layout->addWidget(statusLabel_);

    QFile f(filePath_);
    if (f.open(QIODevice::ReadOnly)) {
        // Cap at 10 MB for display; show first chunk
        QByteArray data = f.read(10 * 1024 * 1024);
        hexView_->setData(data);
        statusLabel_->setText(QString("File: %1   |   %2 bytes")
            .arg(QFileInfo(filePath_).fileName())
            .arg(f.size()));
    } else {
        statusLabel_->setText("Cannot open: " + filePath_);
    }
}

QString HexViewer::title() const {
    return QFileInfo(filePath_).fileName();
}

} // namespace acutesim::gui::editors
