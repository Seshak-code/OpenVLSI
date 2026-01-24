#include "ghost_diff_viewer.h"
#include "../../gui/styling/theme_manager.h"
#include <QTextBlock>

namespace acutesim {
namespace gui {
namespace widgets {

GhostDiffViewer::GhostDiffViewer(QWidget *parent) : QWidget(parent) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Header (Context)
    // We render this inside the editor for seamlessness, or just prepend text?
    // Let's keep it simple for now.

    editor_ = new QPlainTextEdit(this);
    editor_->setReadOnly(true);
    editor_->setFont(QFont("Consolas", 10));
    layout->addWidget(editor_);

    // Action Bar
    auto btnLayout = new QHBoxLayout();
    acceptBtn_ = new QPushButton("Accept (Tab)", this);
    rejectBtn_ = new QPushButton("Reject (Esc)", this);
    
    // Style buttons
    acceptBtn_->setStyleSheet("background-color: #206020; color: white; border: none; padding: 5px;");
    rejectBtn_->setStyleSheet("background-color: #602020; color: white; border: none; padding: 5px;");

    btnLayout->addStretch();
    btnLayout->addWidget(rejectBtn_);
    btnLayout->addWidget(acceptBtn_);
    layout->addLayout(btnLayout);

    connect(acceptBtn_, &QPushButton::clicked, this, &GhostDiffViewer::onAcceptClicked);
    connect(rejectBtn_, &QPushButton::clicked, this, &GhostDiffViewer::onRejectClicked);
}

void GhostDiffViewer::setDiff(const QString& original, const QString& proposed, const QString& contextHeader) {
    header_ = contextHeader;
    renderDiff(original, proposed);
}

void GhostDiffViewer::clear() {
    editor_->clear();
}

void GhostDiffViewer::onAcceptClicked() {
    emit accepted();
}

void GhostDiffViewer::onRejectClicked() {
    emit rejected();
}

void GhostDiffViewer::renderDiff(const QString& original, const QString& proposed) {
    editor_->clear();
    auto& tm = acutesim::gui::styling::ThemeManager::instance();

    // Naive line-based diff for demo
    // In production, use a library or Myers algorithm
    QStringList origLines = original.split('\n');
    QStringList newLines = proposed.split('\n');

    QTextCursor cursor = editor_->textCursor();
    
    // Header
    if (!header_.isEmpty()) {
        QTextCharFormat fmt;
        fmt.setForeground(Qt::gray);
        fmt.setFontItalic(true);
        cursor.insertText(header_ + "\n", fmt);
    }

    // Identify changes (Simplified: If line count differs or content differs completely)
    // For robust diffing, we need tracking. 
    // Here we'll just show "Removed" then "Added" for the whole block if different.
    
    bool varied = (original != proposed);
    
    if (!varied) {
        // No change
         QTextCharFormat fmt;
         fmt.setForeground(tm.getColor("ui.text"));
         cursor.insertText(original, fmt);
         return;
    }

    // Show Removed (Red)
    if (!original.isEmpty()) {
        QTextCharFormat fmt;
        fmt.setBackground(QColor(60, 20, 20)); // Dark Red Background
        fmt.setForeground(QColor(255, 100, 100)); // Light Red Text
        fmt.setFontStrikeOut(true);
        
        for (const QString& line : origLines) {
             cursor.insertText("- " + line + "\n", fmt);
        }
    }

    // Show Added (Green)
    if (!proposed.isEmpty()) {
        QTextCharFormat fmt;
        fmt.setBackground(QColor(20, 60, 20)); // Dark Green Background
        fmt.setForeground(QColor(100, 255, 100)); // Light Green Text
        
        for (const QString& line : newLines) {
             cursor.insertText("+ " + line + "\n", fmt);
        }
    }
}

} // namespace widgets
} // namespace gui
} // namespace acutesim
