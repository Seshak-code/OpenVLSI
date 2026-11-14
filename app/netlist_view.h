#pragma once
// A view that is a netlist's recipe, or a netlist itself. A config view opens as a hierarchy editor: its top, its view list, and every
// cell its hierarchy places with the view it gets and a binding to change that. An extracted view opens read only: each net's
// capacitance and its netlist.
#include "app/session.h"

#include <QJsonObject>
#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;

namespace app {

class NetlistView : public QMainWindow {
    Q_OBJECT
public:
    NetlistView(Session& session, const QString& lib, const QString& cell, const QString& view, QWidget* parent = nullptr);
    QString library() const { return lib_; }
    QString cell() const { return cell_; }
    QString view() const { return view_; }
    bool isConfig() const { return config_; }
    QTableWidget* table() const { return table_; }
    QLineEdit* top() const { return top_; }
    QLineEdit* viewList() const { return list_; }
    QString status() const;
    QString text() const;                      // the netlist shown
    bool apply();                              // a config: writes the top and the view list as typed
    bool bind(int row, const QString& view);   // a config: that row's cell to a view; "" back to the view list
    bool showNetlist();                        // a config: its netlist, below
    void refresh();

private:
    Session& session_;
    QString lib_, cell_, view_;
    bool config_;
    QLineEdit* top_ = nullptr;
    QLineEdit* list_ = nullptr;
    QLabel* status_;
    QTableWidget* table_;
    QPlainTextEdit* text_;
    QJsonObject last_;
};

}  // namespace app
