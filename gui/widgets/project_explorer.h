#pragma once

#include <QWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QMenu>

namespace acutesim {
namespace gui {
namespace widgets {

class ProjectModel;
class ProjectItem;

/**
 * ProjectExplorer - VS Code-style project file browser.
 *
 * Displays a virtual project tree with categories for:
 * - Schematics, HDL, Scripts, Synthesis, Physical Design, Verification
 *
 * Features:
 * - Filter/search box
 * - Context menu for file operations
 * - Double-click to open files
 * - Drag & drop support (future)
 *
 * This widget is "dumb" - it only displays data and emits signals.
 * Business logic (file opening, saving) belongs in gui/core.
 */
class ProjectExplorer : public QWidget {
    Q_OBJECT

public:
    explicit ProjectExplorer(QWidget* parent = nullptr);
    ~ProjectExplorer() override;

    // Access the underlying model
    ProjectModel* model() const { return model_; }

    // Expand/collapse all
    void expandAll();
    void collapseAll();

signals:
    // Emitted when user wants to open a file
    void fileOpenRequested(const QString& category, const QString& fileName, const QString& filePath);

    // Emitted when user wants to create a new file
    void newFileRequested(const QString& category);

    // Emitted when user wants to delete a file
    void deleteFileRequested(const QString& category, const QString& fileName);

    // Emitted when user wants to rename a file
    void renameFileRequested(const QString& category, const QString& oldName);

    // Selection changed
    void selectionChanged(const QString& category, const QString& fileName);

private slots:
    void onItemDoubleClicked(const QModelIndex& index);
    void onContextMenuRequested(const QPoint& pos);
    void onFilterTextChanged(const QString& text);

private:
    void setupUi();
    void setupContextMenu();
    void updateStyle();

    QVBoxLayout* layout_;
    QWidget* headerWidget_;
    QLabel* titleLabel_;
    QToolButton* collapseButton_;
    QToolButton* newFileButton_;
    QLineEdit* filterEdit_;
    QTreeView* treeView_;
    ProjectModel* model_;
    QMenu* contextMenu_;

    // Context menu actions
    QAction* openAction_;
    QAction* newFileAction_;
    QAction* renameAction_;
    QAction* deleteAction_;
};

} // namespace widgets
} // namespace gui
} // namespace acutesim
