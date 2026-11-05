#pragma once
// The side panel's trees over the workspace. Design: libraries, cells and views, with a filter. Simulate: every maestro
// view and its run histories. Reads go through commands; opening and editing are the shell's, given as callbacks.
#include "app/session.h"

#include <QWidget>

#include <functional>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace app {

class Navigator : public QWidget {
    Q_OBJECT
public:
    enum class Mode { Design, Simulate };
    struct Actions {
        std::function<void(const QString& lib, const QString& cell, const QString& view, const QString& history)> open;
        std::function<void(const QString& command, const QJsonObject& prefill)> form;
    };
    Navigator(Session& session, Mode mode, Actions actions, QWidget* parent = nullptr);
    QTreeWidget* tree() const { return tree_; }
    void setFilter(const QString& text);
    QJsonObject selection() const;  // {library, cell, view} as far as the current item names them

public slots:
    void refresh();

private:
    void applyFilter();
    void contextMenu(const QPoint& at);
    Session& session_;
    Mode mode_;
    Actions actions_;
    QLineEdit* filter_;
    QTreeWidget* tree_;
};

}  // namespace app
