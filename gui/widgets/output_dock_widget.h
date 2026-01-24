#pragma once

#include <QDockWidget>
#include <QPlainTextEdit>
#include <QToolBar>
#include <QAction>

class OutputDockWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit OutputDockWidget(const QString& title, QWidget* parent = nullptr);

    void appendText(const QString& text);
    void clear();

    // Accessor for testing
    QPlainTextEdit* editor() const { return textEdit_; }

signals:
    void clearRequested();

private:
    QPlainTextEdit* textEdit_;
    QToolBar* toolbar_;
    QAction* autoScrollAction_;
};
