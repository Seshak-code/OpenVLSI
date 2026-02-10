#include "keybindings_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include "inline_dialog.h"

namespace acutesim {
namespace gui {
namespace widgets {

KeybindingsDialog::KeybindingsDialog(QWidget* parent)
    : QWidget(parent)
    , editingRow_(-1)
{
    setMinimumSize(700, 500);
    setupUi();
    populateTable();
}

void KeybindingsDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);

    // Search bar
    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchBox_ = new QLineEdit(this);
    searchBox_->setPlaceholderText("Search keybindings...");
    searchBox_->setClearButtonEnabled(true);
    connect(searchBox_, &QLineEdit::textChanged, this, &KeybindingsDialog::onSearchChanged);
    searchLayout->addWidget(searchBox_);

    resetAllBtn_ = new QPushButton("Reset All", this);
    connect(resetAllBtn_, &QPushButton::clicked, this, &KeybindingsDialog::onResetAll);
    searchLayout->addWidget(resetAllBtn_);

    mainLayout->addLayout(searchLayout);

    // Table
    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({"Category", "Command", "Keybinding", "Context", ""});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    table_->horizontalHeader()->resizeSection(4, 100);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->setAlternatingRowColors(true);

    // High-density styling
    table_->setStyleSheet(R"(
        QTableWidget {
            background: #1e1e1e;
            alternate-background-color: #252526;
            color: #ccc;
            border: 1px solid #333;
            font-size: 12px;
        }
        QTableWidget::item {
            padding: 4px 8px;
        }
        QTableWidget::item:selected {
            background: #094771;
        }
        QHeaderView::section {
            background: #252526;
            color: #888;
            padding: 6px 8px;
            border: none;
            border-bottom: 1px solid #333;
            font-weight: bold;
        }
    )");

    connect(table_, &QTableWidget::cellDoubleClicked, this, &KeybindingsDialog::onEditBinding);

    mainLayout->addWidget(table_);

    // Status bar
    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet("color: #888; font-size: 11px;");
    mainLayout->addWidget(statusLabel_);

    // Edit widget (hidden initially)
    editWidget_ = new QWidget(this);
    QHBoxLayout* editLayout = new QHBoxLayout(editWidget_);
    editLayout->setContentsMargins(0, 0, 0, 0);
    editLayout->setSpacing(4);

    keyEdit_ = new QKeySequenceEdit(editWidget_);
    keyEdit_->setStyleSheet("QKeySequenceEdit { background: #3c3c3c; color: #fff; border: 1px solid #007acc; padding: 2px 4px; }");
    connect(keyEdit_, &QKeySequenceEdit::keySequenceChanged, this, &KeybindingsDialog::onKeySequenceChanged);
    editLayout->addWidget(keyEdit_);

    acceptBtn_ = new QPushButton("OK", editWidget_);
    acceptBtn_->setFixedWidth(40);
    connect(acceptBtn_, &QPushButton::clicked, this, &KeybindingsDialog::onAcceptEdit);
    editLayout->addWidget(acceptBtn_);

    cancelBtn_ = new QPushButton("X", editWidget_);
    cancelBtn_->setFixedWidth(30);
    connect(cancelBtn_, &QPushButton::clicked, this, &KeybindingsDialog::onCancelEdit);
    editLayout->addWidget(cancelBtn_);

    editWidget_->hide();

    // Dialog styling
    setStyleSheet(R"(
        QDialog {
            background: #1e1e1e;
            color: #ccc;
        }
        QLineEdit {
            background: #3c3c3c;
            color: #fff;
            border: 1px solid #333;
            padding: 6px;
            border-radius: 2px;
        }
        QLineEdit:focus {
            border-color: #007acc;
        }
        QPushButton {
            background: #0e639c;
            color: #fff;
            border: none;
            padding: 6px 12px;
            border-radius: 2px;
        }
        QPushButton:hover {
            background: #1177bb;
        }
        QPushButton:pressed {
            background: #094771;
        }
    )");
}

void KeybindingsDialog::populateTable() {
    populateTableFiltered("");
}

void KeybindingsDialog::populateTableFiltered(const QString& filter) {
    table_->setRowCount(0);

    auto& km = core::KeybindingManager::instance();
    QList<core::KeyBinding> bindings;

    if (filter.isEmpty()) {
        bindings = km.getAllBindings();
    } else {
        bindings = km.search(filter);
    }

    // Sort by category then label
    std::sort(bindings.begin(), bindings.end(), [](const core::KeyBinding& a, const core::KeyBinding& b) {
        if (a.category != b.category) return a.category < b.category;
        return a.label < b.label;
    });

    for (const auto& binding : bindings) {
        int row = table_->rowCount();
        table_->insertRow(row);
        updateRow(row, binding);
    }

    statusLabel_->setText(QString("%1 keybindings").arg(bindings.size()));
}

void KeybindingsDialog::updateRow(int row, const core::KeyBinding& binding) {
    // Category
    QTableWidgetItem* catItem = new QTableWidgetItem(binding.category);
    catItem->setData(Qt::UserRole, binding.id);
    table_->setItem(row, 0, catItem);

    // Label
    QTableWidgetItem* labelItem = new QTableWidgetItem(binding.label);
    if (binding.isCustomized) {
        QFont font = labelItem->font();
        font.setItalic(true);
        labelItem->setFont(font);
        labelItem->setForeground(QColor("#4ec9b0")); // Teal for customized
    }
    table_->setItem(row, 1, labelItem);

    // Keybinding
    QTableWidgetItem* keyItem = new QTableWidgetItem(binding.currentKey.toString(QKeySequence::NativeText));
    if (binding.currentKey.isEmpty()) {
        keyItem->setForeground(QColor("#666"));
        keyItem->setText("(unassigned)");
    }
    table_->setItem(row, 2, keyItem);

    // Context
    QTableWidgetItem* ctxItem = new QTableWidgetItem(binding.context);
    ctxItem->setForeground(QColor("#888"));
    table_->setItem(row, 3, ctxItem);

    // Actions (Edit/Reset buttons)
    QWidget* actionsWidget = new QWidget();
    QHBoxLayout* actionsLayout = new QHBoxLayout(actionsWidget);
    actionsLayout->setContentsMargins(2, 2, 2, 2);
    actionsLayout->setSpacing(2);

    QPushButton* editBtn = new QPushButton("Edit");
    editBtn->setFixedSize(40, 22);
    editBtn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 4px; }");
    connect(editBtn, &QPushButton::clicked, [this, row]() { onEditBinding(row); });
    actionsLayout->addWidget(editBtn);

    QPushButton* resetBtn = new QPushButton("Reset");
    resetBtn->setFixedSize(45, 22);
    resetBtn->setEnabled(binding.isCustomized);
    resetBtn->setStyleSheet("QPushButton { font-size: 10px; padding: 2px 4px; }"
                           "QPushButton:disabled { background: #333; color: #666; }");
    connect(resetBtn, &QPushButton::clicked, [this, row]() { onResetBinding(row); });
    actionsLayout->addWidget(resetBtn);

    table_->setCellWidget(row, 4, actionsWidget);
}

void KeybindingsDialog::onSearchChanged(const QString& text) {
    hideEditWidget();
    populateTableFiltered(text);
}

void KeybindingsDialog::onEditBinding(int row) {
    if (row < 0 || row >= table_->rowCount()) return;

    QTableWidgetItem* item = table_->item(row, 0);
    if (!item) return;

    editingRow_ = row;
    editingId_ = item->data(Qt::UserRole).toString();

    auto& km = core::KeybindingManager::instance();
    core::KeyBinding binding = km.getBinding(editingId_);

    keyEdit_->setKeySequence(binding.currentKey);
    showEditWidget(row);
}

void KeybindingsDialog::showEditWidget(int row) {
    // Position the edit widget over the keybinding cell
    QRect cellRect = table_->visualItemRect(table_->item(row, 2));
    QPoint globalPos = table_->viewport()->mapToGlobal(cellRect.topLeft());
    QPoint localPos = mapFromGlobal(globalPos);

    editWidget_->setGeometry(localPos.x(), localPos.y(), 200, cellRect.height() + 4);
    editWidget_->show();
    editWidget_->raise();
    keyEdit_->setFocus();
}

void KeybindingsDialog::hideEditWidget() {
    editWidget_->hide();
    editingRow_ = -1;
    editingId_.clear();
}

void KeybindingsDialog::onKeySequenceChanged(const QKeySequence& seq) {
    // Check for conflicts
    auto& km = core::KeybindingManager::instance();
    QString conflict = km.findConflict(seq, km.getBinding(editingId_).context);

    if (!conflict.isEmpty() && conflict != editingId_) {
        core::KeyBinding conflictBinding = km.getBinding(conflict);
        statusLabel_->setText(QString("Conflict with: %1 (%2)").arg(conflictBinding.label, conflictBinding.currentKey.toString()));
        statusLabel_->setStyleSheet("color: #f48771; font-size: 11px;");
    } else {
        statusLabel_->setText("");
        statusLabel_->setStyleSheet("color: #888; font-size: 11px;");
    }
}

void KeybindingsDialog::onAcceptEdit() {
    if (editingId_.isEmpty()) return;

    QKeySequence newKey = keyEdit_->keySequence();
    auto& km = core::KeybindingManager::instance();

    if (km.setKeybinding(editingId_, newKey)) {
        core::KeyBinding binding = km.getBinding(editingId_);
        updateRow(editingRow_, binding);
        hideEditWidget();
        statusLabel_->setText("Keybinding updated");
    } else {
        InlineDialog::warning(this, "Conflict", "This keybinding conflicts with another command.");
    }
}

void KeybindingsDialog::onCancelEdit() {
    hideEditWidget();
    statusLabel_->setText("");
}

void KeybindingsDialog::onResetBinding(int row) {
    if (row < 0 || row >= table_->rowCount()) return;

    QTableWidgetItem* item = table_->item(row, 0);
    if (!item) return;

    QString id = item->data(Qt::UserRole).toString();
    auto& km = core::KeybindingManager::instance();

    if (km.resetToDefault(id)) {
        core::KeyBinding binding = km.getBinding(id);
        updateRow(row, binding);
        statusLabel_->setText("Keybinding reset to default");
    }
}

void KeybindingsDialog::onResetAll() {
    InlineDialog::question(this, "Reset All Keybindings",
        "Are you sure you want to reset all keybindings to their defaults?",
        DialogButton::Yes | DialogButton::No,
        [this](const DialogResult& r) {
            if (r.clickedButton == DialogButton::Yes) {
                auto& km = core::KeybindingManager::instance();
                km.resetAllToDefaults();
                populateTableFiltered(searchBox_->text());
                statusLabel_->setText("All keybindings reset to defaults");
            }
        });
}

} // namespace widgets
} // namespace gui
} // namespace acutesim
