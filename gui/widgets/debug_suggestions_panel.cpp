#include "debug_suggestions_panel.h"
#include "../../ai_api/context/context_engine.h"
#include <QScrollArea>
#include <QApplication>

namespace acutesim::gui::widgets {

DebugSuggestionsPanel::DebugSuggestionsPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void DebugSuggestionsPanel::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    
    // Header with icon
    headerLabel_ = new QLabel("🔍 <b>Debug Suggestions</b>");
    headerLabel_->setStyleSheet("color: #4EC9B0; font-size: 14px;");
    layout->addWidget(headerLabel_);
    
    // Info label
    QLabel* infoLabel = new QLabel("AI-recommended probe points based on circuit analysis:");
    infoLabel->setStyleSheet("color: #888; font-size: 11px;");
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);
    
    // Suggestions list
    suggestionsList_ = new QListWidget();
    suggestionsList_->setStyleSheet(
        "QListWidget { background-color: #1E1E1E; border: 1px solid #3E3E42; }"
        "QListWidget::item { padding: 8px; border-bottom: 1px solid #2D2D30; }"
        "QListWidget::item:hover { background-color: #2D2D30; }"
        "QListWidget::item:selected { background-color: #094771; }"
    );
    suggestionsList_->setAlternatingRowColors(false);
    connect(suggestionsList_, &QListWidget::itemDoubleClicked, 
            this, &DebugSuggestionsPanel::onSuggestionClicked);
    layout->addWidget(suggestionsList_, 1);
    
    // Button row
    QHBoxLayout* btnLayout = new QHBoxLayout();
    
    placeAllBtn_ = new QPushButton("📍 Place All");
    placeAllBtn_->setStyleSheet(
        "QPushButton { background-color: #28A745; color: white; border: none; padding: 6px 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #218838; }"
        "QPushButton:disabled { background-color: #555; }"
    );
    connect(placeAllBtn_, &QPushButton::clicked, this, &DebugSuggestionsPanel::onPlaceAllClicked);
    btnLayout->addWidget(placeAllBtn_);
    
    refreshBtn_ = new QPushButton("🔄 Refresh");
    refreshBtn_->setStyleSheet(
        "QPushButton { background-color: #007ACC; color: white; border: none; padding: 6px 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #005A9E; }"
    );
    connect(refreshBtn_, &QPushButton::clicked, this, &DebugSuggestionsPanel::onRefreshClicked);
    btnLayout->addWidget(refreshBtn_);
    
    btnLayout->addStretch();
    layout->addLayout(btnLayout);
    
    // Initial state
    placeAllBtn_->setEnabled(false);
}

void DebugSuggestionsPanel::refreshSuggestions() {
    // Get debug suggestions from ContextEngine
    QJsonArray suggestions = AIAPI::ContextEngine::getDebugSuggestions();
    currentSuggestions_ = suggestions;
    populateList(suggestions);
    
    placeAllBtn_->setEnabled(!suggestions.isEmpty());
}

void DebugSuggestionsPanel::populateList(const QJsonArray& suggestions) {
    suggestionsList_->clear();
    
    if (suggestions.isEmpty()) {
        QListWidgetItem* emptyItem = new QListWidgetItem("✅ No issues detected. Circuit looks good!");
        emptyItem->setForeground(QColor("#28A745"));
        suggestionsList_->addItem(emptyItem);
        return;
    }
    
    for (const auto& sugVal : suggestions) {
        QJsonObject sug = sugVal.toObject();
        int priority = sug["priority"].toInt(3);
        QString node = sug["node"].toString();
        QString reason = sug["reason"].toString();
        
        QString icon = getPriorityIcon(priority);
        QString color = getPriorityColor(priority);
        
        QListWidgetItem* item = new QListWidgetItem(
            QString("%1 %2\n   %3").arg(icon).arg(node).arg(reason)
        );
        item->setForeground(QColor(color));
        item->setData(Qt::UserRole, node);  // Store node for placement
        item->setData(Qt::UserRole + 1, reason);
        item->setToolTip(QString("Double-click to place probe at %1").arg(node));
        
        suggestionsList_->addItem(item);
    }
}

QString DebugSuggestionsPanel::getPriorityIcon(int priority) const {
    switch (priority) {
        case 1: return "🔴";  // Critical
        case 2: return "🟡";  // Medium
        case 3: return "🔵";  // Low/Info
        default: return "⚪";
    }
}

QString DebugSuggestionsPanel::getPriorityColor(int priority) const {
    switch (priority) {
        case 1: return "#DC3545";  // Red
        case 2: return "#FFC107";  // Yellow
        case 3: return "#17A2B8";  // Cyan
        default: return "#888888";
    }
}

void DebugSuggestionsPanel::onSuggestionClicked(QListWidgetItem* item) {
    QString node = item->data(Qt::UserRole).toString();
    QString reason = item->data(Qt::UserRole + 1).toString();
    
    if (!node.isEmpty()) {
        placeProbeAtNode(node);
        emit probePlaced(node, reason);
    }
}

void DebugSuggestionsPanel::onPlaceAllClicked() {
    if (!editor_) return;
    
    for (const auto& sugVal : currentSuggestions_) {
        QJsonObject sug = sugVal.toObject();
        QString node = sug["node"].toString();
        placeProbeAtNode(node);
    }
    
    // Visual feedback
    headerLabel_->setText("🔍 <b>Debug Suggestions</b> <span style='color:#28A745;'>(All Placed!)</span>");
    placeAllBtn_->setEnabled(false);
}

void DebugSuggestionsPanel::onRefreshClicked() {
    headerLabel_->setText("🔍 <b>Debug Suggestions</b>");
    refreshSuggestions();
}

void DebugSuggestionsPanel::placeProbeAtNode(const QString& node) {
    if (!editor_) return;
    
    // Find node center in schematic
    QPointF nodeCenter = editor_->findNodeCenter(node);
    
    if (!nodeCenter.isNull()) {
        // Place probe at node - positive tip at node, negative at ground
        QPointF probeBody(nodeCenter.x(), nodeCenter.y() - 100);
        QPointF posTip = nodeCenter;
        QPointF negTip(nodeCenter.x() + 50, nodeCenter.y() + 50);  // Offset for visibility
        
        editor_->placeProbe("", probeBody, posTip, negTip);
    }
}

} // namespace acutesim::gui::widgets
