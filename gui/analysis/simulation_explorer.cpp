#include "simulation_explorer.h"
#include <QHeaderView>
#include <QStyle>

SimulationExplorer::SimulationExplorer(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    
    treeWidget_ = new QTreeWidget(this);
    treeWidget_->setHeaderHidden(true);
    treeWidget_->setRootIsDecorated(true);
    treeWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Style
    treeWidget_->setStyleSheet(R"(
        QTreeWidget {
            border: none;
            background-color: #252526;
            color: #cccccc;
            font-size: 12px;
        }
        QTreeWidget::item {
            padding: 4px 2px;
        }
        QTreeWidget::item:selected {
            background-color: #094771;
        }
        QTreeWidget::item:hover {
            background-color: #2a2d2e;
        }
        QTreeWidget::branch:has-children:!has-siblings:closed,
        QTreeWidget::branch:closed:has-children:has-siblings {
            image: url(:/icons/chevron-right.png);
        }
        QTreeWidget::branch:open:has-children:!has-siblings,
        QTreeWidget::branch:open:has-children:has-siblings {
            image: url(:/icons/chevron-down.png);
        }
    )");
    
    setupTree();
    
    connect(treeWidget_, &QTreeWidget::itemDoubleClicked, 
            this, &SimulationExplorer::onItemDoubleClicked);
    connect(treeWidget_, &QTreeWidget::customContextMenuRequested,
            this, &SimulationExplorer::showContextMenu);
    
    layout->addWidget(treeWidget_);
}

void SimulationExplorer::setupTree() {
    // Create category nodes
    quickChecksNode_ = new QTreeWidgetItem(treeWidget_, {"Quick Checks"});
    quickChecksNode_->setIcon(0, style()->standardIcon(QStyle::SP_MediaPlay));
    quickChecksNode_->setExpanded(true);
    
    analysesNode_ = new QTreeWidgetItem(treeWidget_, {"Analyses"});
    analysesNode_->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    analysesNode_->setExpanded(true);
    
    sweepsNode_ = new QTreeWidgetItem(treeWidget_, {"Sweeps"});
    sweepsNode_->setIcon(0, style()->standardIcon(QStyle::SP_BrowserReload));
    
    cornersNode_ = new QTreeWidgetItem(treeWidget_, {"Corners"});
    cornersNode_->setIcon(0, style()->standardIcon(QStyle::SP_ComputerIcon));
    
    historyNode_ = new QTreeWidgetItem(treeWidget_, {"History"});
    historyNode_->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogInfoView));
    
    // Add default profiles
    addProfile(Simulation::SimulationProfile::quickDC());
    addProfile(Simulation::SimulationProfile::transient());
    addProfile(Simulation::SimulationProfile::acSweep());
}

QTreeWidgetItem* SimulationExplorer::getCategoryNode(Simulation::SimulationProfile::Category cat) {
    switch (cat) {
        case Simulation::SimulationProfile::Category::QUICK: return quickChecksNode_;
        case Simulation::SimulationProfile::Category::ANALYSIS: return analysesNode_;
        case Simulation::SimulationProfile::Category::SWEEP: return sweepsNode_;
        case Simulation::SimulationProfile::Category::CORNER: return cornersNode_;
        default: return analysesNode_;
    }
}

void SimulationExplorer::addProfile(const Simulation::SimulationProfile& profile) {
    QTreeWidgetItem* parent = getCategoryNode(profile.category);
    QTreeWidgetItem* item = new QTreeWidgetItem(parent, {profile.name});
    item->setData(0, Qt::UserRole, profile.profileId);
    item->setIcon(0, getStatusIcon(profile.status));
    item->setToolTip(0, QString("Analysis: %1")
        .arg(Simulation::analysisToString(profile.analysis)));
}

void SimulationExplorer::updateProfileStatus(const QString& profileId, 
                                              Simulation::SimulationProfile::Status status) {
    // Find and update the item
    for (int i = 0; i < treeWidget_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* cat = treeWidget_->topLevelItem(i);
        for (int j = 0; j < cat->childCount(); ++j) {
            QTreeWidgetItem* item = cat->child(j);
            if (item->data(0, Qt::UserRole).toString() == profileId) {
                item->setIcon(0, getStatusIcon(status));
                return;
            }
        }
    }
}

void SimulationExplorer::markAllStale() {
    // Mark all profiles as STALE
    for (int i = 0; i < treeWidget_->topLevelItemCount() - 1; ++i) { // Skip History
        QTreeWidgetItem* cat = treeWidget_->topLevelItem(i);
        for (int j = 0; j < cat->childCount(); ++j) {
            QTreeWidgetItem* item = cat->child(j);
            item->setIcon(0, getStatusIcon(Simulation::SimulationProfile::Status::STALE));
        }
    }
}

void SimulationExplorer::addRun(const Simulation::SimulationRun& run) {
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setText(0, run.toSummary());
    item->setData(0, Qt::UserRole, run.runId);
    item->setData(0, Qt::UserRole + 1, "run"); // Tag as run, not profile
    item->setIcon(0, getRunStatusIcon(run.status));
    
    // Insert at top of history
    historyNode_->insertChild(0, item);
    
    // Limit history to 50 items
    while (historyNode_->childCount() > 50) {
        delete historyNode_->takeChild(historyNode_->childCount() - 1);
    }
}

void SimulationExplorer::clearHistory() {
    while (historyNode_->childCount() > 0) {
        delete historyNode_->takeChild(0);
    }
}

QString SimulationExplorer::selectedProfileId() const {
    QTreeWidgetItem* item = treeWidget_->currentItem();
    if (!item) return QString();
    
    // Check if it's a profile (not a category or run)
    QString tag = item->data(0, Qt::UserRole + 1).toString();
    if (tag == "run") return QString(); // It's a run
    if (!item->parent()) return QString(); // It's a category
    
    return item->data(0, Qt::UserRole).toString();
}

void SimulationExplorer::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item || !item->parent()) return; // Ignore categories
    
    QString tag = item->data(0, Qt::UserRole + 1).toString();
    if (tag == "run") return; // Don't run history items
    
    QString profileId = item->data(0, Qt::UserRole).toString();
    if (!profileId.isEmpty()) {
        emit runRequested(profileId);
    }
}

void SimulationExplorer::showContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = treeWidget_->itemAt(pos);
    if (!item || !item->parent()) return;
    
    QString profileId = item->data(0, Qt::UserRole).toString();
    QString tag = item->data(0, Qt::UserRole + 1).toString();
    
    QMenu* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    if (tag != "run") {
        // Profile context menu
        QAction* runAction = menu->addAction("Run");
        connect(runAction, &QAction::triggered, this, [this, profileId]() {
            emit runRequested(profileId);
        });

        menu->addSeparator();

        QAction* dupAction = menu->addAction("Duplicate");
        connect(dupAction, &QAction::triggered, this, [this, profileId]() {
            emit profileDuplicateRequested(profileId);
        });

        QAction* delAction = menu->addAction("Delete");
        connect(delAction, &QAction::triggered, this, [this, profileId]() {
            emit profileDeleteRequested(profileId);
        });
    } else {
        // Run history context menu
        QAction* viewAction = menu->addAction("View Results");
        // TODO: Connect to results viewer
        Q_UNUSED(viewAction);
    }

    menu->popup(treeWidget_->mapToGlobal(pos));
}

QIcon SimulationExplorer::getStatusIcon(Simulation::SimulationProfile::Status status) {
    switch (status) {
        case Simulation::SimulationProfile::Status::READY:
            return style()->standardIcon(QStyle::SP_MediaPlay);
        case Simulation::SimulationProfile::Status::STALE:
            return style()->standardIcon(QStyle::SP_MessageBoxWarning);
        case Simulation::SimulationProfile::Status::RUNNING:
            return style()->standardIcon(QStyle::SP_BrowserReload);
        case Simulation::SimulationProfile::Status::PASSED:
            return style()->standardIcon(QStyle::SP_DialogApplyButton);
        case Simulation::SimulationProfile::Status::FAILED:
            return style()->standardIcon(QStyle::SP_DialogCancelButton);
        default:
            return QIcon();
    }
}

QIcon SimulationExplorer::getRunStatusIcon(Simulation::SimulationRun::Status status) {
    switch (status) {
        case Simulation::SimulationRun::Status::CONVERGED:
            return style()->standardIcon(QStyle::SP_DialogApplyButton);
        case Simulation::SimulationRun::Status::FAILED:
            return style()->standardIcon(QStyle::SP_DialogCancelButton);
        case Simulation::SimulationRun::Status::RUNNING:
            return style()->standardIcon(QStyle::SP_BrowserReload);
        default:
            return style()->standardIcon(QStyle::SP_MediaPlay);
    }
}
