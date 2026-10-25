#pragma once
// The Library Manager (Virtuoso's): libraries, their cells, a cell's views, in three columns. Every action is a
// command through the session (reads too), so an agent can do the same and the CIW echoes the changes. The dialog
// slots only gather names; the actions below are what they (and the tests) call.
#include "app/session.h"

#include <QWidget>

class QListWidget;

namespace app {

class LibraryManager : public QWidget {
    Q_OBJECT
public:
    explicit LibraryManager(Session& session, QWidget* parent = nullptr);

    // the actions: true when the command succeeded
    bool createLibrary(const QString& name, const QString& path);
    bool defineLibrary(const QString& name, const QString& path);
    bool createCell(const QString& name);                          // in the selected library
    bool createView(const QString& name, const QString& type);    // in the selected cell
    bool renameCell(const QString& to);                           // the selected cell
    bool deleteSelected();                                        // the selected view, else the selected cell
    bool undo();                                                  // in the selected library
    bool redo();

    void select(const QString& library, const QString& cell = {}, const QString& view = {});
    QString selectedLibrary() const;
    QString selectedCell() const;
    QString selectedView() const;
    QStringList libraries() const;
    QStringList cells() const;
    QStringList views() const;  // "name (type)"

public slots:
    void refresh();  // from the database (another process may have changed it)

protected:
    void changeEvent(QEvent* e) override;

private:
    void fillCells();
    void fillViews();
    static QStringList items(const QListWidget* w);
    static QString current(const QListWidget* w);
    Session& session_;
    QListWidget *libs_, *cells_, *views_;
    bool filling_ = false;
};

}  // namespace app
