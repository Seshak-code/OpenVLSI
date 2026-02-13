#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QJsonArray>
#include <QJsonObject>
#include "../editors/schematic/schematic_editor.h"

namespace acutesim::gui::widgets {

/**
 * DebugSuggestionsPanel - AI-recommended probe placement panel.
 * 
 * Displays a priority-sorted list of recommended probe points based on
 * circuit topology analysis. Users can click to auto-place probes.
 */
class DebugSuggestionsPanel : public QWidget {
    Q_OBJECT

public:
    explicit DebugSuggestionsPanel(QWidget* parent = nullptr);
    
    /**
     * Set the schematic editor context for probe placement.
     */
    void setEditor(SchematicEditor* editor) { editor_ = editor; }
    
    /**
     * Refresh suggestions based on current circuit state.
     */
    void refreshSuggestions();

signals:
    /**
     * Emitted when a probe is placed from a suggestion.
     */
    void probePlaced(const QString& node, const QString& reason);

private slots:
    void onSuggestionClicked(QListWidgetItem* item);
    void onPlaceAllClicked();
    void onRefreshClicked();

private:
    SchematicEditor* editor_ = nullptr;
    
    QLabel* headerLabel_;
    QListWidget* suggestionsList_;
    QPushButton* placeAllBtn_;
    QPushButton* refreshBtn_;
    
    QJsonArray currentSuggestions_;
    
    void setupUi();
    void populateList(const QJsonArray& suggestions);
    void placeProbeAtNode(const QString& node);
    
    QString getPriorityIcon(int priority) const;
    QString getPriorityColor(int priority) const;
};

} // namespace acutesim::gui::widgets
