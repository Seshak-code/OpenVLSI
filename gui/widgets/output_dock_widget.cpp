#include "output_dock_widget.h"
#include <QVBoxLayout>
#include <QScrollBar>
#include "../../gui/styling/theme_manager.h"

OutputDockWidget::OutputDockWidget(const QString& title, QWidget* parent) 
    : QDockWidget(title, parent) 
{
    // Container to hold toolbar + editor
    QWidget* container = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar (Right-aligned in VS Code usually, here we put it top)
    toolbar_ = new QToolBar(container);
    toolbar_->setIconSize(QSize(16, 16));
    // Style toolbar to blend in
    auto& tm = acutesim::gui::styling::ThemeManager::instance();
    toolbar_->setStyleSheet(QString("QToolBar { background: %1; border-bottom: 1px solid %2; }")
                            .arg(tm.getColor("ui.header").name())
                            .arg(tm.getColor("ui.border").name()));

    QAction* clearAct = toolbar_->addAction("Clear");
    clearAct->setIcon(QIcon::fromTheme("edit-clear")); 
    connect(clearAct, &QAction::triggered, this, &OutputDockWidget::clear);

    autoScrollAction_ = toolbar_->addAction("Auto-Scroll");
    autoScrollAction_->setCheckable(true);
    autoScrollAction_->setChecked(true);

    layout->addWidget(toolbar_);

    // Editor
    textEdit_ = new QPlainTextEdit(container);
    textEdit_->setReadOnly(true);
    textEdit_->setStyleSheet("font-family: Consolas, monospace; border: none; font-size: 9pt;");
    layout->addWidget(textEdit_);

    setWidget(container);
}

void OutputDockWidget::appendText(const QString& text) {
    textEdit_->appendPlainText(text);
    if (autoScrollAction_->isChecked()) {
        textEdit_->verticalScrollBar()->setValue(textEdit_->verticalScrollBar()->maximum());
    }
}

void OutputDockWidget::clear() {
    textEdit_->clear();
    emit clearRequested();
}
