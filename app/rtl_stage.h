#pragma once
// The RTL stage: a digital design's sources. A panel lists designs and their files; a file opens as a tab: Verilog, highlighted, checked with the design's other files, and from there simulated or implemented.
#include "app/session.h"

#include <QJsonObject>
#include <QMainWindow>
#include <QWidget>

#include <functional>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

namespace app {

class RtlEditor : public QMainWindow {
    Q_OBJECT
public:
    RtlEditor(Session& session, const QString& file, const QString& design, QWidget* parent = nullptr);
    QString file() const { return file_; }
    QString design() const { return design_; }
    QString text() const;
    void setText(const QString& text);
    bool modified() const;
    bool save();                    // false with why in status()
    QJsonObject lint();             // rtl_lint on the design, or on this file
    QString status() const;
    int diagnosticCount() const;
    void goToLine(int line);
    int currentLine() const;

signals:
    void implementRequested(const QString& design);
    void simulateRequested(const QString& design);

private:
    Session& session_;
    QString file_, design_;
    QPlainTextEdit* editor_;
    QListWidget* problems_;
    QLabel* status_;
};

class RtlPanel : public QWidget {
    Q_OBJECT
public:
    struct Actions {
        std::function<void(const QString& file, const QString& design)> open;
        std::function<void()> addDesign;
    };
    RtlPanel(Session& session, Actions actions, QWidget* parent = nullptr);
    void refresh();
    QTreeWidget* tree() const { return tree_; }

private:
    Session& session_;
    Actions actions_;
    QTreeWidget* tree_;
};

}  // namespace app
