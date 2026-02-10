#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QKeySequenceEdit>
#include "gui/core/keybinding_manager.h"

namespace acutesim {
namespace gui {
namespace widgets {

/**
 * KeybindingsDialog - VS Code-style keyboard shortcuts editor
 *
 * Now a QWidget for use inside InlineDialog::showCustom().
 *
 * Features:
 * - Searchable list of all shortcuts
 * - Inline editing of shortcuts
 * - Conflict detection and warnings
 * - Reset individual or all shortcuts
 * - Category grouping
 */
class KeybindingsDialog : public QWidget {
    Q_OBJECT

public:
    explicit KeybindingsDialog(QWidget* parent = nullptr);

private slots:
    void onSearchChanged(const QString& text);
    void onEditBinding(int row);
    void onResetBinding(int row);
    void onResetAll();
    void onKeySequenceChanged(const QKeySequence& seq);
    void onAcceptEdit();
    void onCancelEdit();

private:
    void setupUi();
    void populateTable();
    void populateTableFiltered(const QString& filter);
    void updateRow(int row, const acutesim::gui::core::KeyBinding& binding);
    void showEditWidget(int row);
    void hideEditWidget();

    QLineEdit* searchBox_;
    QTableWidget* table_;
    QPushButton* resetAllBtn_;
    QLabel* statusLabel_;

    // Edit mode state
    QWidget* editWidget_;
    QKeySequenceEdit* keyEdit_;
    QPushButton* acceptBtn_;
    QPushButton* cancelBtn_;
    int editingRow_;
    QString editingId_;
};

} // namespace widgets
} // namespace gui
} // namespace acutesim
