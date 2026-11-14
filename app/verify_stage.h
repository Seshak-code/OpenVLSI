#pragma once
// The Verify stage: DRC and LVS runs of layouts. A DRC opens on its layout with its markers; an LVS says what it compared, and one
// that differs opens on its layout with what differs marked.
#include "app/session.h"

#include <QJsonObject>
#include <QWidget>

#include <functional>

class QLabel;
class QTimer;
class QTreeWidget;

namespace app {

class VerifyPanel : public QWidget {
    Q_OBJECT
public:
    struct Actions {
        std::function<void(const QString& command)> form;  // starts a run: a form, prefilled from the selection
    };
    VerifyPanel(Session& session, Actions actions, QWidget* parent = nullptr);
    void refresh();
    QTreeWidget* runs() const { return tree_; }
    QString details() const;
    bool open(const QString& run);  // a DRC on its layout, an LVS's summary

private:
    Session& session_;
    Actions actions_;
    QTreeWidget* tree_;
    QLabel* details_;
    QTimer* poll_;
};

}  // namespace app
