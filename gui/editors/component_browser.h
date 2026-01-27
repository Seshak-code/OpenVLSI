#pragma once

#include <QDockWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QSplitter>
#include <QFileSystemWatcher>
#include <QTimer>
#include "../components/component_library.h"

/**
 * ComponentBrowser
 * 
 * A dockable component library browser for loading saved subcircuits.
 * 
 * Design Goals:
 * - Compatible with current Qt docking system
 * - Future-proof for VS Code-style UI refresh (Phase 11)
 * - AI API integration ready
 * - Mode-organized (Bench/Block/System)
 */
class ComponentBrowser : public QDockWidget {
    Q_OBJECT

public:
    explicit ComponentBrowser(QWidget* parent = nullptr);
    
    // Refresh the component list from disk
    void refreshLibrary();
    
    // Get currently selected component name (for insertion)
    QString selectedComponentName() const;
    
signals:
    // Emitted when user wants to insert a component
    void insertComponentRequested(const QString& componentPath);
    
    // Emitted when selection changes (for preview updates)
    void selectionChanged(const QString& componentPath);

private slots:
    void onSearchTextChanged(const QString& text);
    void onItemSelectionChanged();
    void onInsertClicked();
    void onRefreshClicked();
    void onFileSystemChanged(const QString& path);

private:
    void setupUI();
    void populateTree();
    void updatePreview(const QString& componentPath);
    
    // UI Components
    QWidget* mainWidget;
    QLineEdit* searchBox;
    QTreeWidget* treeWidget;
    QLabel* previewLabel;
    QLabel* pinListLabel;
    QPushButton* insertButton;
    QPushButton* refreshButton;
    
    // File system watcher for auto-refresh
    QFileSystemWatcher* fsWatcher;
    
    // Cached component list
    std::vector<ComponentEntry> componentCache;
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

inline ComponentBrowser::ComponentBrowser(QWidget* parent)
    : QDockWidget(tr("Component Library"), parent)
{
    setObjectName("ComponentBrowser");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    setupUI();
    refreshLibrary();
    
    // Watch library directory for changes
    fsWatcher = new QFileSystemWatcher(this);
    fsWatcher->addPath(QString::fromStdString(getLibraryPath()));
    connect(fsWatcher, &QFileSystemWatcher::directoryChanged,
            this, &ComponentBrowser::onFileSystemChanged);
}

inline void ComponentBrowser::setupUI() {
    mainWidget = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);
    
    // Search box
    auto* searchLayout = new QHBoxLayout();
    searchBox = new QLineEdit();
    searchBox->setPlaceholderText(tr("Search components..."));
    searchBox->setClearButtonEnabled(true);
    connect(searchBox, &QLineEdit::textChanged,
            this, &ComponentBrowser::onSearchTextChanged);
    searchLayout->addWidget(searchBox);
    
    refreshButton = new QPushButton(tr("↻"));
    refreshButton->setToolTip(tr("Refresh library"));
    refreshButton->setMaximumWidth(30);
    connect(refreshButton, &QPushButton::clicked,
            this, &ComponentBrowser::onRefreshClicked);
    searchLayout->addWidget(refreshButton);
    
    mainLayout->addLayout(searchLayout);
    
    // Splitter for tree and preview
    auto* splitter = new QSplitter(Qt::Vertical);
    
    // Tree widget
    treeWidget = new QTreeWidget();
    treeWidget->setHeaderLabel(tr("Components"));
    treeWidget->setRootIsDecorated(false);
    treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(treeWidget, &QTreeWidget::itemSelectionChanged,
            this, &ComponentBrowser::onItemSelectionChanged);
    connect(treeWidget, &QTreeWidget::itemDoubleClicked,
            this, &ComponentBrowser::onInsertClicked);
    
    splitter->addWidget(treeWidget);
    
    // Preview panel
    auto* previewGroup = new QGroupBox(tr("Preview"));
    auto* previewLayout = new QVBoxLayout(previewGroup);
    
    previewLabel = new QLabel(tr("Select a component"));
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setMinimumHeight(80);
    previewLabel->setStyleSheet("QLabel { background-color: #2d2d2d; border: 1px solid #3d3d3d; border-radius: 4px; }");
    previewLayout->addWidget(previewLabel);
    
    pinListLabel = new QLabel();
    pinListLabel->setWordWrap(true);
    previewLayout->addWidget(pinListLabel);
    
    splitter->addWidget(previewGroup);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);
    
    mainLayout->addWidget(splitter);
    
    // Insert button
    insertButton = new QPushButton(tr("Insert Component"));
    insertButton->setEnabled(false);
    connect(insertButton, &QPushButton::clicked,
            this, &ComponentBrowser::onInsertClicked);
    mainLayout->addWidget(insertButton);
    
    setWidget(mainWidget);
}

inline void ComponentBrowser::refreshLibrary() {
    componentCache = listLibraryComponents();
    populateTree();
}

inline void ComponentBrowser::populateTree() {
    treeWidget->clear();
    
    QString filter = searchBox->text().toLower();
    
    for (const auto& comp : componentCache) {
        QString name = QString::fromStdString(comp.name);
        
        // Apply search filter
        if (!filter.isEmpty() && !name.toLower().contains(filter))
            continue;
        
        auto* item = new QTreeWidgetItem(treeWidget);
        item->setText(0, name);
        item->setData(0, Qt::UserRole, QString::fromStdString(comp.filepath));
        item->setIcon(0, QIcon::fromTheme("package-x-generic"));
    }
}

inline void ComponentBrowser::onSearchTextChanged(const QString& /*text*/) {
    populateTree();
}

inline void ComponentBrowser::onItemSelectionChanged() {
    auto items = treeWidget->selectedItems();
    if (items.isEmpty()) {
        insertButton->setEnabled(false);
        previewLabel->setText(tr("Select a component"));
        pinListLabel->clear();
        return;
    }
    
    QString path = items.first()->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) {
        insertButton->setEnabled(false);
        return;
    }
    
    insertButton->setEnabled(true);
    updatePreview(path);
    emit selectionChanged(path);
}

inline void ComponentBrowser::onInsertClicked() {
    auto items = treeWidget->selectedItems();
    if (items.isEmpty()) return;
    
    QString path = items.first()->data(0, Qt::UserRole).toString();
    if (!path.isEmpty()) {
        emit insertComponentRequested(path);
    }
}

inline void ComponentBrowser::onRefreshClicked() {
    refreshLibrary();
}

inline void ComponentBrowser::onFileSystemChanged(const QString& /*path*/) {
    // Debounce rapid changes
    QTimer::singleShot(100, this, &ComponentBrowser::refreshLibrary);
}

inline QString ComponentBrowser::selectedComponentName() const {
    auto items = treeWidget->selectedItems();
    if (items.isEmpty()) return QString();
    return items.first()->text(0);
}

inline void ComponentBrowser::updatePreview(const QString& /*componentPath*/) {
    // TODO: Parse .comp file and show visual preview
    // For now, show placeholder with component name
    auto items = treeWidget->selectedItems();
    if (items.isEmpty()) return;
    
    QString name = items.first()->text(0);
    previewLabel->setText(QString("📦 %1").arg(name));
    pinListLabel->setText(tr("Pins: (loading...)"));
}
