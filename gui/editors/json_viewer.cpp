#include "json_viewer.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QHeaderView>
#include "gui/styling/theme_manager.h"

namespace acutesim::gui::editors {

JsonViewer::JsonViewer(const QString& filePath, QWidget* parent)
    : core::IEditor(parent), filePath_(filePath)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({"Key", "Value"});
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->setAlternatingRowColors(false);

    errorLabel_ = new QLabel(this);
    errorLabel_->setWordWrap(true);
    errorLabel_->setVisible(false);

    layout->addWidget(errorLabel_);
    layout->addWidget(tree_);

    loadJson();
}

QString JsonViewer::title() const {
    return QFileInfo(filePath_).fileName();
}

void JsonViewer::loadJson() {
    QFile f(filePath_);
    if (!f.open(QIODevice::ReadOnly)) {
        errorLabel_->setText("Cannot open file: " + filePath_);
        errorLabel_->setVisible(true);
        tree_->setVisible(false);
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull()) {
        errorLabel_->setText("JSON parse error: " + err.errorString());
        errorLabel_->setVisible(true);
        tree_->setVisible(false);
        return;
    }

    tree_->clear();
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            auto* item = new QTreeWidgetItem(tree_, {it.key()});
            populateItem(item, it.key(), it.value());
        }
    } else if (doc.isArray()) {
        QJsonArray arr = doc.array();
        for (int i = 0; i < arr.size(); ++i) {
            auto* item = new QTreeWidgetItem(tree_, {QString("[%1]").arg(i)});
            populateItem(item, QString::number(i), arr[i]);
        }
    }
    tree_->expandToDepth(1);
}

void JsonViewer::populateItem(QTreeWidgetItem* parent, const QString& key, const QJsonValue& value) {
    Q_UNUSED(key)
    auto& tm = acutesim::gui::styling::ThemeManager::instance();

    switch (value.type()) {
    case QJsonValue::Object: {
        parent->setText(1, "{…}");
        parent->setForeground(1, tm.getColor("ui.textDim"));
        QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            auto* child = new QTreeWidgetItem(parent, {it.key()});
            populateItem(child, it.key(), it.value());
        }
        break;
    }
    case QJsonValue::Array: {
        QJsonArray arr = value.toArray();
        parent->setText(1, QString("[%1 items]").arg(arr.size()));
        parent->setForeground(1, tm.getColor("ui.textDim"));
        for (int i = 0; i < arr.size(); ++i) {
            auto* child = new QTreeWidgetItem(parent, {QString("[%1]").arg(i)});
            populateItem(child, QString::number(i), arr[i]);
        }
        break;
    }
    case QJsonValue::String:
        parent->setText(1, "\"" + value.toString() + "\"");
        parent->setForeground(1, QColor("#ce9178")); // orange
        break;
    case QJsonValue::Double:
        parent->setText(1, QString::number(value.toDouble(), 'g', 10));
        parent->setForeground(1, QColor("#b5cea8")); // green
        break;
    case QJsonValue::Bool:
        parent->setText(1, value.toBool() ? "true" : "false");
        parent->setForeground(1, QColor("#569cd6")); // blue
        break;
    case QJsonValue::Null:
        parent->setText(1, "null");
        parent->setForeground(1, tm.getColor("ui.textDim"));
        break;
    default:
        parent->setText(1, value.toVariant().toString());
        break;
    }
}

} // namespace acutesim::gui::editors
