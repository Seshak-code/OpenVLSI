#include "search_dock.h"
#include "schematic_editor.h"
#include "../styling/theme_manager.h"
#include <QHeaderView>

namespace acutesim::gui::widgets {

SearchDock::SearchDock(QWidget* parent) : QWidget(parent) {
    setupUi();

    // Debounce timer to prevent excessive searches
    debounceTimer_ = new QTimer(this);
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(300);
    connect(debounceTimer_, &QTimer::timeout, this, &SearchDock::performSearch);
}

void SearchDock::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // Search input
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search components, nets...");
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setStyleSheet(R"(
        QLineEdit {
            background: #3c3c3c;
            color: #ccc;
            border: 1px solid #454545;
            border-radius: 4px;
            padding: 8px 12px;
            font-size: 13px;
        }
        QLineEdit:focus {
            border-color: #007ACC;
        }
    )");
    connect(searchEdit_, &QLineEdit::textChanged, this, &SearchDock::onSearchTextChanged);
    layout->addWidget(searchEdit_);

    // Category filter
    categoryCombo_ = new QComboBox(this);
    categoryCombo_->addItem("All", "all");
    categoryCombo_->addItem("Components", "components");
    categoryCombo_->addItem("Nets", "nets");
    categoryCombo_->setStyleSheet(R"(
        QComboBox {
            background: #3c3c3c;
            color: #ccc;
            border: 1px solid #454545;
            border-radius: 4px;
            padding: 6px 10px;
            font-size: 12px;
        }
        QComboBox:hover {
            border-color: #555;
        }
        QComboBox::drop-down {
            border: none;
            width: 20px;
        }
        QComboBox QAbstractItemView {
            background: #252526;
            color: #ccc;
            selection-background-color: #094771;
        }
    )");
    connect(categoryCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { performSearch(); });
    layout->addWidget(categoryCombo_);

    // Results tree
    resultsTree_ = new QTreeWidget(this);
    resultsTree_->setHeaderLabels({"Name", "Type"});
    resultsTree_->setColumnWidth(0, 180);
    resultsTree_->setRootIsDecorated(true);
    resultsTree_->setAlternatingRowColors(false);
    resultsTree_->setStyleSheet(R"(
        QTreeWidget {
            background: #1e1e1e;
            color: #ccc;
            border: 1px solid #3c3c3c;
            border-radius: 4px;
            font-size: 12px;
            outline: none;
        }
        QTreeWidget::item {
            padding: 4px 8px;
        }
        QTreeWidget::item:hover {
            background: #2a2d2e;
        }
        QTreeWidget::item:selected {
            background: #094771;
        }
        QHeaderView::section {
            background: #252526;
            color: #888;
            border: none;
            padding: 6px 8px;
            font-size: 11px;
        }
    )");
    connect(resultsTree_, &QTreeWidget::itemDoubleClicked,
            this, &SearchDock::onResultDoubleClicked);
    layout->addWidget(resultsTree_, 1);

    // Status label
    statusLabel_ = new QLabel("Enter search term", this);
    statusLabel_->setStyleSheet("color: #666; font-size: 11px;");
    layout->addWidget(statusLabel_);
}

void SearchDock::setSchematicEditor(SchematicEditor* editor) {
    editor_ = editor;
    // Refresh search if there's an active query
    if (!searchEdit_->text().isEmpty()) {
        performSearch();
    }
}

void SearchDock::onSearchTextChanged(const QString& text) {
    Q_UNUSED(text);
    // Restart debounce timer
    debounceTimer_->start();
}

void SearchDock::performSearch() {
    QString query = searchEdit_->text().trimmed().toLower();
    resultsTree_->clear();

    if (query.isEmpty()) {
        statusLabel_->setText("Enter search term");
        return;
    }

    QString category = categoryCombo_->currentData().toString();

    if (category == "all" || category == "components") {
        searchComponents(query);
    }

    if (category == "all" || category == "nets") {
        searchNets(query);
    }

    int totalResults = 0;
    for (int i = 0; i < resultsTree_->topLevelItemCount(); ++i) {
        totalResults += resultsTree_->topLevelItem(i)->childCount();
    }

    statusLabel_->setText(QString("%1 result%2 found")
                         .arg(totalResults)
                         .arg(totalResults == 1 ? "" : "s"));
}

void SearchDock::searchComponents(const QString& query) {
    if (!editor_) return;

    QGraphicsScene* scene = editor_->getScene();
    if (!scene) return;

    QTreeWidgetItem* componentRoot = new QTreeWidgetItem(resultsTree_);
    componentRoot->setText(0, "Components");
    componentRoot->setExpanded(true);

    int count = 0;
    for (QGraphicsItem* item : scene->items()) {
        VisualComponent* comp = dynamic_cast<VisualComponent*>(item);
        if (comp) {
            QString name = comp->name.toLower();
            QString value = comp->value.toLower();

            if (name.contains(query) || value.contains(query)) {
                QTreeWidgetItem* resultItem = new QTreeWidgetItem(componentRoot);
                resultItem->setText(0, comp->name);
                resultItem->setText(1, comp->value);
                resultItem->setData(0, Qt::UserRole, "component");
                resultItem->setData(0, Qt::UserRole + 1, comp->name);

                // Store position for navigation
                QPointF pos = comp->scenePos();
                resultItem->setData(0, Qt::UserRole + 2, pos.x());
                resultItem->setData(0, Qt::UserRole + 3, pos.y());

                count++;
            }
        }
    }

    if (count == 0) {
        delete componentRoot;
    } else {
        componentRoot->setText(0, QString("Components (%1)").arg(count));
    }
}

void SearchDock::searchNets(const QString& query) {
    if (!editor_) return;

    QGraphicsScene* scene = editor_->getScene();
    if (!scene) return;

    QTreeWidgetItem* netRoot = new QTreeWidgetItem(resultsTree_);
    netRoot->setText(0, "Nets");
    netRoot->setExpanded(true);

    QSet<QString> addedNets;
    int count = 0;

    for (QGraphicsItem* item : scene->items()) {
        VisualWire* wire = dynamic_cast<VisualWire*>(item);
        if (wire) {
            QString netName = wire->getName().toLower();

            if (netName.contains(query) && !addedNets.contains(wire->getName())) {
                addedNets.insert(wire->getName());

                QTreeWidgetItem* resultItem = new QTreeWidgetItem(netRoot);
                resultItem->setText(0, wire->getName());
                resultItem->setText(1, "Net");
                resultItem->setData(0, Qt::UserRole, "net");
                resultItem->setData(0, Qt::UserRole + 1, wire->getName());

                // Store position for navigation
                QPointF center = wire->line().center();
                resultItem->setData(0, Qt::UserRole + 2, center.x());
                resultItem->setData(0, Qt::UserRole + 3, center.y());

                count++;
            }
        }
    }

    if (count == 0) {
        delete netRoot;
    } else {
        netRoot->setText(0, QString("Nets (%1)").arg(count));
    }
}

void SearchDock::onResultDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);

    if (!item || !editor_) return;

    QString type = item->data(0, Qt::UserRole).toString();
    if (type.isEmpty()) return; // Category header, not a result

    QString name = item->data(0, Qt::UserRole + 1).toString();
    qreal x = item->data(0, Qt::UserRole + 2).toReal();
    qreal y = item->data(0, Qt::UserRole + 3).toReal();

    // Center view on the item
    editor_->centerOn(x, y);

    // Emit navigation signal
    if (type == "component") {
        emit navigateToComponent(name);
    } else if (type == "net") {
        emit navigateToNet(name);
    }
}

} // namespace acutesim::gui::widgets
