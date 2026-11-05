#pragma once
// ADE Verifier's window: the requirements tree, each with its status and what proves it, and the proof of the selected one.
#include "app/session.h"

#include <QJsonObject>
#include <QMainWindow>

class QLabel;
class QPlainTextEdit;
class QTimer;
class QTreeWidget;

namespace app {

class Verifier : public QMainWindow {
    Q_OBJECT
public:
    Verifier(Session& session, const QString& lib, const QString& cell, const QString& view, QWidget* parent = nullptr);
    QString library() const { return lib_; }
    QString cell() const { return cell_; }
    QString view() const { return view_; }
    QTreeWidget* tree() const { return tree_; }
    QString status() const;
    QString derivation() const;
    void check();           // re-reads every history
    int runUnverified();    // starts the mapped ADE views whose requirements are not proved; how many started

private:
    void addRequirement();
    void mapRequirement();
    void removeRequirement();
    void fillDerivation();
    QString selectedId() const;
    Session& session_;
    QString lib_, cell_, view_;
    QTreeWidget* tree_;
    QPlainTextEdit* derivation_;
    QLabel* status_;
    QTimer* poll_;
    QJsonObject last_;
};

}  // namespace app
