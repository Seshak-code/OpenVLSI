#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QTreeWidget>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

class SchematicEditor;

namespace acutesim::gui::widgets {

class SearchDock : public QWidget {
    Q_OBJECT
public:
    explicit SearchDock(QWidget* parent = nullptr);
    void setSchematicEditor(SchematicEditor* editor);

signals:
    void navigateToComponent(const QString& componentName);
    void navigateToNet(const QString& netName);

private slots:
    void onSearchTextChanged(const QString& text);
    void performSearch();
    void onResultDoubleClicked(QTreeWidgetItem* item, int column);

private:
    void setupUi();
    void searchComponents(const QString& query);
    void searchNets(const QString& query);

    QLineEdit* searchEdit_;
    QComboBox* categoryCombo_;
    QTreeWidget* resultsTree_;
    QLabel* statusLabel_;
    SchematicEditor* editor_ = nullptr;
    QTimer* debounceTimer_;
};

} // namespace acutesim::gui::widgets
