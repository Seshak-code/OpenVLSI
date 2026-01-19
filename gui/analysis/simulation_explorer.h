#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QMenu>
#include <QAction>
#include "simulation_profile.h"

/**
 * SimulationExplorer - Tree view for simulation profiles & history
 * 
 * Part of Phase 1.7: VS Code-style Simulation Suite.
 * Similar to VS Code's Explorer or Cadence ADE's analysis list.
 */
class SimulationExplorer : public QWidget {
    Q_OBJECT
    
public:
    explicit SimulationExplorer(QWidget* parent = nullptr);
    
    // Profile management
    void addProfile(const Simulation::SimulationProfile& profile);
    void updateProfileStatus(const QString& profileId, Simulation::SimulationProfile::Status status);
    void markAllStale();
    
    // History management
    void addRun(const Simulation::SimulationRun& run);
    void clearHistory();
    
    // Get selected profile
    QString selectedProfileId() const;
    
signals:
    void profileSelected(const QString& profileId);
    void runRequested(const QString& profileId);
    void profileDuplicateRequested(const QString& profileId);
    void profileDeleteRequested(const QString& profileId);
    void runDiffRequested(const QString& runId1, const QString& runId2);
    
private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void showContextMenu(const QPoint& pos);
    
private:
    QTreeWidget* treeWidget_;
    
    // Category nodes
    QTreeWidgetItem* quickChecksNode_;
    QTreeWidgetItem* analysesNode_;
    QTreeWidgetItem* sweepsNode_;
    QTreeWidgetItem* cornersNode_;
    QTreeWidgetItem* historyNode_;
    
    void setupTree();
    QTreeWidgetItem* getCategoryNode(Simulation::SimulationProfile::Category cat);
    QIcon getStatusIcon(Simulation::SimulationProfile::Status status);
    QIcon getRunStatusIcon(Simulation::SimulationRun::Status status);
};
