#pragma once

#include "gui/core/editor_manager.h"
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QLabel>

namespace acutesim::gui::editors {

/**
 * @brief Read-only JSON tree viewer.
 *
 * Parses a JSON file and displays it as a collapsible tree.
 * Keys in column 0, values in column 1, type-coloured.
 */
class JsonViewer : public core::IEditor {
    Q_OBJECT
public:
    explicit JsonViewer(const QString& filePath, QWidget* parent = nullptr);

    QString filePath() const override { return filePath_; }
    QString title() const override;
    bool isDirty() const override { return false; }
    core::EditorCapabilities capabilities() const override { return {}; }

private:
    void loadJson();
    void populateItem(QTreeWidgetItem* parent, const QString& key, const QJsonValue& value);

    QString filePath_;
    QTreeWidget* tree_ = nullptr;
    QLabel* errorLabel_ = nullptr;
};

} // namespace acutesim::gui::editors
