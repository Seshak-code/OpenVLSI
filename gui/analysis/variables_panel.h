#pragma once
#include <QAbstractTableModel>
#include <QDockWidget>
#include <QTableView>
#include <QToolBar>
#include "engine_api/simulation_context.h"

namespace acutesim::gui::analysis {

// ─── Table Model ─────────────────────────────────────────────────────────────
class VariablesModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit VariablesModel(QObject* parent = nullptr);

    // Context synchronization
    void setContext(acutesim::orchestration::SimulationContext* ctx);
    void applyToContext(acutesim::orchestration::SimulationContext* ctx) const;

    // QAbstractItemModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Operations
    void addVariable(const QString& name, const QString& expr);
    void removeVariable(int row);
    void revalidate(); // Trigger topological sort & eval via Context

signals:
    void variablesChanged();

private:
    struct VariableEntry {
        QString name;
        QString expression;
        double resolvedValue = 0.0; // Cached from last valid resolve
        bool enabled = true;
        bool isValid = true;
        QString errorMsg;
    };
    
    QVector<VariableEntry> entries_;
    acutesim::orchestration::SimulationContext* ctx_ = nullptr; // Pointer to live context
};

// ─── Dock Widget ─────────────────────────────────────────────────────────────
class VariablesPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit VariablesPanel(QWidget* parent = nullptr);
    
    void setContext(acutesim::orchestration::SimulationContext* ctx);

signals:
    void contextModified();
    void sweepRequested(); // [NEW]

private slots:
    void addVariable();
    void removeSelected();
    void onModelChanged();

private:
    VariablesModel* model_;
    QTableView* view_;
    QToolBar* toolbar_;
};

} // namespace acutesim::gui::analysis
