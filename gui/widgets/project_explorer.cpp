#include "project_explorer.h"
#include "project_model.h"
#include "gui/core/icon_provider.h"
#include "gui/styling/theme_manager.h"
#include <QHeaderView>
#include <QDebug>
#include <QSortFilterProxyModel>

namespace acutesim {
namespace gui {
namespace widgets {

ProjectExplorer::ProjectExplorer(QWidget* parent)
    : QWidget(parent)
    , model_(new ProjectModel(this))
    , contextMenu_(new QMenu(this)) {
    setupUi();
    setupContextMenu();
    updateStyle();

    // Connect to theme changes
    connect(&styling::ThemeManager::instance(), &styling::ThemeManager::themeChanged,
            this, &ProjectExplorer::updateStyle);
}

ProjectExplorer::~ProjectExplorer() = default;

void ProjectExplorer::setupUi() {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    // Header with title and buttons
    headerWidget_ = new QWidget();
    auto headerLayout = new QHBoxLayout(headerWidget_);
    headerLayout->setContentsMargins(8, 6, 8, 6);
    headerLayout->setSpacing(4);

    titleLabel_ = new QLabel("EXPLORER");
    titleLabel_->setObjectName("explorerTitle");
    headerLayout->addWidget(titleLabel_);

    headerLayout->addStretch();

    // New file button
    newFileButton_ = new QToolButton();
    newFileButton_->setIcon(core::IconProvider::icon("file"));
    newFileButton_->setToolTip("New File...");
    newFileButton_->setAutoRaise(true);
    newFileButton_->setIconSize(QSize(16, 16));
    connect(newFileButton_, &QToolButton::clicked, this, [this]() {
        // Get currently selected category
        QModelIndex current = treeView_->currentIndex();
        if (current.isValid()) {
            ProjectItem* item = model_->itemFromIndex(current);
            if (item) {
                QString category = item->type() == ProjectItem::ItemType::Folder
                    ? item->name()
                    : (item->parent() ? item->parent()->name() : "Schematics");
                emit newFileRequested(category);
            }
        } else {
            emit newFileRequested("Schematics");
        }
    });
    headerLayout->addWidget(newFileButton_);

    // Collapse all button
    collapseButton_ = new QToolButton();
    collapseButton_->setIcon(core::IconProvider::icon("folder"));
    collapseButton_->setToolTip("Collapse All");
    collapseButton_->setAutoRaise(true);
    collapseButton_->setIconSize(QSize(16, 16));
    connect(collapseButton_, &QToolButton::clicked, this, &ProjectExplorer::collapseAll);
    headerLayout->addWidget(collapseButton_);

    layout_->addWidget(headerWidget_);

    // Filter/search box
    filterEdit_ = new QLineEdit();
    filterEdit_->setPlaceholderText("Filter files...");
    filterEdit_->setClearButtonEnabled(true);
    filterEdit_->setObjectName("explorerFilter");
    connect(filterEdit_, &QLineEdit::textChanged, this, &ProjectExplorer::onFilterTextChanged);
    layout_->addWidget(filterEdit_);

    // Tree view
    treeView_ = new QTreeView();
    treeView_->setModel(model_);
    treeView_->setHeaderHidden(true);
    treeView_->setAnimated(true);
    treeView_->setIndentation(16);
    treeView_->setExpandsOnDoubleClick(false); // We handle double-click ourselves
    treeView_->setContextMenuPolicy(Qt::CustomContextMenu);
    treeView_->setSelectionMode(QAbstractItemView::SingleSelection);

    // Expand top-level folders by default
    treeView_->expandAll();

    connect(treeView_, &QTreeView::doubleClicked, this, &ProjectExplorer::onItemDoubleClicked);
    connect(treeView_, &QTreeView::customContextMenuRequested, this, &ProjectExplorer::onContextMenuRequested);
    connect(treeView_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
        if (!current.isValid()) return;
        ProjectItem* item = model_->itemFromIndex(current);
        if (item && item->parent()) {
            emit selectionChanged(item->parent()->name(), item->name());
        }
    });

    layout_->addWidget(treeView_, 1);
}

void ProjectExplorer::setupContextMenu() {
    openAction_ = contextMenu_->addAction("Open");
    connect(openAction_, &QAction::triggered, this, [this]() {
        QModelIndex index = treeView_->currentIndex();
        if (index.isValid()) {
            onItemDoubleClicked(index);
        }
    });

    contextMenu_->addSeparator();

    newFileAction_ = contextMenu_->addAction("New File...");
    connect(newFileAction_, &QAction::triggered, this, [this]() {
        QModelIndex index = treeView_->currentIndex();
        if (index.isValid()) {
            ProjectItem* item = model_->itemFromIndex(index);
            QString category = item->type() == ProjectItem::ItemType::Folder
                ? item->name()
                : (item->parent() ? item->parent()->name() : "Schematics");
            emit newFileRequested(category);
        }
    });

    renameAction_ = contextMenu_->addAction("Rename...");
    connect(renameAction_, &QAction::triggered, this, [this]() {
        QModelIndex index = treeView_->currentIndex();
        if (index.isValid()) {
            ProjectItem* item = model_->itemFromIndex(index);
            if (item && item->type() != ProjectItem::ItemType::Folder &&
                item->type() != ProjectItem::ItemType::Root) {
                QString category = item->parent() ? item->parent()->name() : "";
                emit renameFileRequested(category, item->name());
            }
        }
    });

    contextMenu_->addSeparator();

    deleteAction_ = contextMenu_->addAction("Delete");
    connect(deleteAction_, &QAction::triggered, this, [this]() {
        QModelIndex index = treeView_->currentIndex();
        if (index.isValid()) {
            ProjectItem* item = model_->itemFromIndex(index);
            if (item && item->type() != ProjectItem::ItemType::Folder &&
                item->type() != ProjectItem::ItemType::Root) {
                QString category = item->parent() ? item->parent()->name() : "";
                emit deleteFileRequested(category, item->name());
            }
        }
    });
}

void ProjectExplorer::onItemDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;

    ProjectItem* item = model_->itemFromIndex(index);
    if (!item) return;

    // If it's a folder, toggle expand/collapse
    if (item->type() == ProjectItem::ItemType::Folder ||
        item->type() == ProjectItem::ItemType::Root) {
        treeView_->setExpanded(index, !treeView_->isExpanded(index));
        return;
    }

    // It's a file - emit open request
    QString category = item->parent() ? item->parent()->name() : "";
    emit fileOpenRequested(category, item->name(), item->path());
}

void ProjectExplorer::onContextMenuRequested(const QPoint& pos) {
    QModelIndex index = treeView_->indexAt(pos);

    // Update action states based on selection
    bool isFile = false;
    bool isFolder = false;

    if (index.isValid()) {
        ProjectItem* item = model_->itemFromIndex(index);
        if (item) {
            isFile = (item->type() != ProjectItem::ItemType::Folder &&
                      item->type() != ProjectItem::ItemType::Root);
            isFolder = (item->type() == ProjectItem::ItemType::Folder);
        }
    }

    openAction_->setEnabled(isFile);
    newFileAction_->setEnabled(isFile || isFolder);
    renameAction_->setEnabled(isFile);
    deleteAction_->setEnabled(isFile);

    contextMenu_->popup(treeView_->viewport()->mapToGlobal(pos));
}

void ProjectExplorer::onFilterTextChanged(const QString& text) {
    // Simple filter: expand all and highlight matches
    // A more sophisticated implementation would use QSortFilterProxyModel
    Q_UNUSED(text);
    // For now, just expand all when filtering
    if (!text.isEmpty()) {
        treeView_->expandAll();
    }
}

void ProjectExplorer::expandAll() {
    treeView_->expandAll();
}

void ProjectExplorer::collapseAll() {
    treeView_->collapseAll();
}

void ProjectExplorer::updateStyle() {
    auto& tm = styling::ThemeManager::instance();
    QString bg = tm.getColor("ui.sidebar").name();
    QString text = tm.getColor("ui.text").name();
    QString textDim = tm.getColor("ui.textDim").name();
    QString border = tm.getColor("ui.border").name();
    QString accent = tm.getColor("ui.accent").name();
    QString hoverBg = tm.getColor("ui.hoverBackground").name();

    setStyleSheet(QString(R"(
        ProjectExplorer {
            background-color: %1;
        }
        #explorerTitle {
            color: %3;
            font-size: 11px;
            font-weight: 600;
            letter-spacing: 1px;
        }
        #explorerFilter {
            background-color: %1;
            border: 1px solid %4;
            border-radius: 3px;
            padding: 4px 8px;
            margin: 4px 8px;
            color: %2;
        }
        #explorerFilter:focus {
            border-color: %5;
        }
        QTreeView {
            background-color: %1;
            border: none;
            color: %2;
            outline: none;
        }
        QTreeView::item {
            padding: 4px 2px;
            border-radius: 3px;
        }
        QTreeView::item:hover {
            background-color: %6;
        }
        QTreeView::item:selected {
            background-color: %5;
            color: white;
        }
        QTreeView::branch {
            background-color: %1;
        }
        QToolButton {
            background: transparent;
            border: none;
            padding: 4px;
            border-radius: 3px;
        }
        QToolButton:hover {
            background-color: %6;
        }
    )").arg(bg, text, textDim, border, accent, hoverBg));
}

} // namespace widgets
} // namespace gui
} // namespace acutesim
