#include "variables_panel.h"
#include <QHeaderView>
#include <QVBoxLayout>
#include <QAction>

namespace acutesim::gui::analysis {

// =============================================================================
// VariablesModel
// =============================================================================

VariablesModel::VariablesModel(QObject* parent) : QAbstractTableModel(parent) {}

void VariablesModel::setContext(acutesim::orchestration::SimulationContext* ctx) {
    beginResetModel();
    ctx_ = ctx;
    entries_.clear();
    
    if (ctx_) {
        // Load from context
        auto keys = ctx_->variableExprs.keys();
        // Sort keys for stability
        std::sort(keys.begin(), keys.end());
        
        for (const auto& key : keys) {
            VariableEntry e;
            e.name = key;
            e.expression = ctx_->variableExprs.value(key);
            e.enabled = ctx_->variableEnabled.value(key, true);
            e.resolvedValue = ctx_->variables.value(key, 0.0);
            entries_.append(e);
        }
    }
    endResetModel();
}

void VariablesModel::applyToContext(acutesim::orchestration::SimulationContext* ctx) const {
    if (!ctx) return;
    
    ctx->variableExprs.clear();
    ctx->variables.clear(); // Will be re-populated by resolve
    ctx->variableEnabled.clear();
    
    for (const auto& e : entries_) {
        ctx->variableExprs.insert(e.name, e.expression);
        ctx->variableEnabled.insert(e.name, e.enabled);
    }
    
    // Trigger resolution in context
    ctx->resolveVariables();
}

int VariablesModel::rowCount(const QModelIndex&) const { return entries_.size(); }
int VariablesModel::columnCount(const QModelIndex&) const { return 4; } // Name, Expr, Value, Enabled

QVariant VariablesModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();
    switch (section) {
        case 0: return "Name";
        case 1: return "Expression";
        case 2: return "Value";
        case 3: return "Enabled";
        default: return QVariant();
    }
}

QVariant VariablesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= entries_.size()) return QVariant();
    const auto& e = entries_[index.row()];
    
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case 0: return e.name;
            case 1: return e.expression;
            case 2: return QString::number(e.resolvedValue, 'g', 6);
            case 3: return e.enabled;
        }
    }
    
    if (role == Qt::CheckStateRole && index.column() == 3) {
        return e.enabled ? Qt::Checked : Qt::Unchecked;
    }
    
    if (role == Qt::ToolTipRole && !e.isValid) {
        return e.errorMsg;
    }
    
    if (role == Qt::BackgroundRole && !e.isValid) {
        return QColor(255, 200, 200); // Light red for error
    }
    
    return QVariant();
}

bool VariablesModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid()) return false;
    auto& e = entries_[index.row()];
    
    if (role == Qt::CheckStateRole && index.column() == 3) {
        e.enabled = (value.toInt() == Qt::Checked);
    } else if (role == Qt::EditRole) {
        switch (index.column()) {
            case 0: e.name = value.toString(); break;
            case 1: e.expression = value.toString(); break;
            case 3: e.enabled = value.toBool(); break;
            default: return false;
        }
    } else {
        return false;
    }
    
    emit dataChanged(index, index);
    revalidate(); // Re-resolve immediately
    return true;
}

Qt::ItemFlags VariablesModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    
    auto flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() != 2) flags |= Qt::ItemIsEditable; // Value is read-only
    if (index.column() == 3) flags |= Qt::ItemIsUserCheckable;
    
    return flags;
}

void VariablesModel::addVariable(const QString& name, const QString& expr) {
    beginInsertRows(QModelIndex(), entries_.size(), entries_.size());
    entries_.append({name, expr});
    endInsertRows();
    revalidate();
}

void VariablesModel::removeVariable(int row) {
    if (row < 0 || row >= entries_.size()) return;
    beginRemoveRows(QModelIndex(), row, row);
    entries_.removeAt(row);
    endRemoveRows();
    revalidate();
}

void VariablesModel::revalidate() {
    // 1. Sync local model to temp SimContext
    if (!ctx_) return;
    
    applyToContext(ctx_);
    
    // 2. Resolve
    QString errorMsg;
    bool ok = ctx_->resolveVariables(&errorMsg);
    
    // 3. Update Model with resolved values and statuses
    for (auto& e : entries_) {
        if (ok) {
            e.resolvedValue = ctx_->variables.value(e.name, 0.0);
            e.isValid = true;
            e.errorMsg.clear();
        } else {
            // For now, if global resolve fails, mark all as potential suspects?
            // Or ideally ctx->resolveVariables returns per-variable status.
            // Simplified: Mark all invalid if global fail.
            e.isValid = false; 
            e.errorMsg = errorMsg;
        }
    }
    
    emit dataChanged(index(0, 0), index(entries_.size()-1, 3));
    emit variablesChanged();
}

// =============================================================================
// VariablesPanel
// =============================================================================

VariablesPanel::VariablesPanel(QWidget* parent) : QDockWidget("Design Variables", parent) {
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0,0,0,0);
    
    toolbar_ = new QToolBar(this);
    auto* actAdd = toolbar_->addAction("Add");
    auto* actDel = toolbar_->addAction("Remove");
    
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(spacer);
    
    auto* actSweep = toolbar_->addAction("Sweep...");
    connect(actSweep, &QAction::triggered, this, &VariablesPanel::sweepRequested);
    
    connect(actAdd, &QAction::triggered, this, &VariablesPanel::addVariable);
    connect(actDel, &QAction::triggered, this, &VariablesPanel::removeSelected);
    
    model_ = new VariablesModel(this);
    view_ = new QTableView(this);
    view_->setModel(model_);
    view_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    
    layout->addWidget(toolbar_);
    layout->addWidget(view_);
    setWidget(container);
    
    connect(model_, &VariablesModel::variablesChanged, this, &VariablesPanel::onModelChanged);
}

void VariablesPanel::setContext(acutesim::orchestration::SimulationContext* ctx) {
    model_->setContext(ctx);
    // Initial resolve
    model_->revalidate(); 
}

void VariablesPanel::addVariable() {
    static int counter = 1;
    model_->addVariable(QString("var%1").arg(counter++), "0");
}

void VariablesPanel::removeSelected() {
    auto idx = view_->selectionModel()->currentIndex();
    if (idx.isValid()) {
        model_->removeVariable(idx.row());
    }
}

void VariablesPanel::onModelChanged() {
    emit contextModified();
}

} // namespace acutesim::gui::analysis
