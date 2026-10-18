#include <memory>
#include "project_model.h"
#include "gui/core/icon_provider.h"
#include <QJsonArray>
#include <QDebug>

namespace acutesim {
namespace gui {
namespace widgets {

// ============================================================================
// ProjectItem Implementation
// ============================================================================

ProjectItem::ProjectItem(const QString& name, ItemType type, ProjectItem* parent)
    : name_(name), type_(type), parent_(parent) {}

ProjectItem::~ProjectItem() = default;

void ProjectItem::appendChild(std::unique_ptr<ProjectItem> child) {
    children_.push_back(std::move(child));
}

ProjectItem* ProjectItem::child(int row) const {
    if (row < 0 || row >= static_cast<int>(children_.size())) return nullptr;
    return children_.at(static_cast<size_t>(row)).get();
}

int ProjectItem::childCount() const {
    return static_cast<int>(children_.size());
}

int ProjectItem::row() const {
    if (!parent_) return 0;
    for (size_t i = 0; i < parent_->children_.size(); ++i) {
        if (parent_->children_.at(i).get() == this) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

ProjectItem* ProjectItem::parent() const {
    return parent_;
}

QIcon ProjectItem::icon() const {
    using namespace acutesim::gui::core;

    switch (type_) {
        case ItemType::Root:
            return IconProvider::icon("explorer");
        case ItemType::Folder:
            return IconProvider::icon("folder");
        case ItemType::SchematicFile:
            return IconProvider::icon("schematic");
        case ItemType::HDLFile:
            return IconProvider::icon("hdl");
        case ItemType::ScriptFile:
            return IconProvider::icon("file");
        case ItemType::SynthesisFile:
            return IconProvider::icon("synthesis");
        case ItemType::PhysicalFile:
            return IconProvider::icon("physical");
        case ItemType::VerificationFile:
            return IconProvider::icon("verification");
        default:
            return IconProvider::icon("file");
    }
}

// ============================================================================
// ProjectModel Implementation
// ============================================================================

ProjectModel::ProjectModel(QObject* parent)
    : QAbstractItemModel(parent), projectName_("Untitled Project") {
    setupDefaultStructure();
}

ProjectModel::~ProjectModel() = default;

void ProjectModel::setupDefaultStructure() {
    rootItem_ = std::make_unique<ProjectItem>(projectName_, ProjectItem::ItemType::Root);

    // Create default folder structure
    auto schematics = std::make_unique<ProjectItem>("Schematics", ProjectItem::ItemType::Folder, rootItem_.get());
    auto hdl = std::make_unique<ProjectItem>("HDL", ProjectItem::ItemType::Folder, rootItem_.get());
    auto scripts = std::make_unique<ProjectItem>("Scripts", ProjectItem::ItemType::Folder, rootItem_.get());
    auto synthesis = std::make_unique<ProjectItem>("Synthesis", ProjectItem::ItemType::Folder, rootItem_.get());
    auto physical = std::make_unique<ProjectItem>("Physical Design", ProjectItem::ItemType::Folder, rootItem_.get());
    auto verification = std::make_unique<ProjectItem>("Verification", ProjectItem::ItemType::Folder, rootItem_.get());

    rootItem_->appendChild(std::move(schematics));
    rootItem_->appendChild(std::move(hdl));
    rootItem_->appendChild(std::move(scripts));
    rootItem_->appendChild(std::move(synthesis));
    rootItem_->appendChild(std::move(physical));
    rootItem_->appendChild(std::move(verification));
}

QModelIndex ProjectModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent)) return QModelIndex();

    ProjectItem* parentItem = parent.isValid()
        ? static_cast<ProjectItem*>(parent.internalPointer())
        : rootItem_.get();

    ProjectItem* childItem = parentItem->child(row);
    if (childItem) {
        return createIndex(row, column, childItem);
    }
    return QModelIndex();
}

QModelIndex ProjectModel::parent(const QModelIndex& index) const {
    if (!index.isValid()) return QModelIndex();

    ProjectItem* childItem = static_cast<ProjectItem*>(index.internalPointer());
    ProjectItem* parentItem = childItem->parent();

    if (!parentItem || parentItem == rootItem_.get()) {
        return QModelIndex();
    }

    return createIndex(parentItem->row(), 0, parentItem);
}

int ProjectModel::rowCount(const QModelIndex& parent) const {
    if (parent.column() > 0) return 0;

    ProjectItem* parentItem = parent.isValid()
        ? static_cast<ProjectItem*>(parent.internalPointer())
        : rootItem_.get();

    return parentItem->childCount();
}

int ProjectModel::columnCount(const QModelIndex& /*parent*/) const {
    return 1; // Single column (name only)
}

QVariant ProjectModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return QVariant();

    ProjectItem* item = static_cast<ProjectItem*>(index.internalPointer());

    switch (role) {
        case Qt::DisplayRole:
            return item->name();
        case Qt::DecorationRole:
            return item->icon();
        case Qt::ToolTipRole:
            if (!item->path().isEmpty()) {
                return item->path();
            }
            return item->name();
        default:
            return QVariant();
    }
}

Qt::ItemFlags ProjectModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;

    ProjectItem* item = static_cast<ProjectItem*>(index.internalPointer());

    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

    // Files can be dragged, folders can accept drops
    if (item->type() != ProjectItem::ItemType::Folder &&
        item->type() != ProjectItem::ItemType::Root) {
        flags |= Qt::ItemIsDragEnabled;
    }

    return flags;
}

void ProjectModel::setProjectName(const QString& name) {
    projectName_ = name;
    if (rootItem_) {
        rootItem_->setName(name);
    }
    emit projectChanged();
}

QString ProjectModel::projectName() const {
    return projectName_;
}

ProjectItem* ProjectModel::findCategory(const QString& categoryName) const {
    if (!rootItem_) return nullptr;

    for (int i = 0; i < rootItem_->childCount(); ++i) {
        ProjectItem* child = rootItem_->child(i);
        if (child && child->name() == categoryName) {
            return child;
        }
    }
    return nullptr;
}

bool ProjectModel::addFile(const QString& category, const QString& fileName, const QString& filePath) {
    ProjectItem* categoryItem = findCategory(category);
    if (!categoryItem) {
        qWarning() << "[ProjectModel] Category not found:" << category;
        return false;
    }

    // Determine file type from category
    ProjectItem::ItemType fileType = ProjectItem::ItemType::ScriptFile;
    if (category == "Schematics") {
        fileType = ProjectItem::ItemType::SchematicFile;
    } else if (category == "HDL") {
        fileType = ProjectItem::ItemType::HDLFile;
    } else if (category == "Scripts") {
        fileType = ProjectItem::ItemType::ScriptFile;
    } else if (category == "Synthesis") {
        fileType = ProjectItem::ItemType::SynthesisFile;
    } else if (category == "Physical Design") {
        fileType = ProjectItem::ItemType::PhysicalFile;
    } else if (category == "Verification") {
        fileType = ProjectItem::ItemType::VerificationFile;
    }

    // Find the category's model index
    QModelIndex categoryIndex;
    for (int i = 0; i < rootItem_->childCount(); ++i) {
        if (rootItem_->child(i) == categoryItem) {
            categoryIndex = createIndex(i, 0, categoryItem);
            break;
        }
    }

    int newRow = categoryItem->childCount();
    beginInsertRows(categoryIndex, newRow, newRow);

    auto newFile = std::make_unique<ProjectItem>(fileName, fileType, categoryItem);
    newFile->setPath(filePath);
    categoryItem->appendChild(std::move(newFile));

    endInsertRows();

    emit fileAdded(category, fileName);
    emit projectChanged();
    return true;
}

bool ProjectModel::removeFile(const QModelIndex& index) {
    if (!index.isValid()) return false;

    ProjectItem* item = static_cast<ProjectItem*>(index.internalPointer());
    if (!item || item->type() == ProjectItem::ItemType::Folder ||
        item->type() == ProjectItem::ItemType::Root) {
        return false; // Can't remove folders
    }

    ProjectItem* parentItem = item->parent();
    if (!parentItem) return false;

    QString category = parentItem->name();
    QString fileName = item->name();

    beginRemoveRows(parent(index), index.row(), index.row());
    // Note: actual removal happens via parent's children list
    // This is a simplified implementation - in production, add proper removal
    endRemoveRows();

    emit fileRemoved(category, fileName);
    emit projectChanged();
    return true;
}

ProjectItem* ProjectModel::itemFromIndex(const QModelIndex& index) const {
    if (!index.isValid()) return rootItem_.get();
    return static_cast<ProjectItem*>(index.internalPointer());
}

void ProjectModel::clear() {
    beginResetModel();
    setupDefaultStructure();
    endResetModel();
    emit projectChanged();
}

QJsonObject ProjectModel::toJson() const {
    QJsonObject root;
    root["projectName"] = projectName_;
    root["version"] = 1;

    QJsonArray categories;
    for (int i = 0; i < rootItem_->childCount(); ++i) {
        categories.append(itemToJson(rootItem_->child(i)));
    }
    root["categories"] = categories;

    return root;
}

QJsonObject ProjectModel::itemToJson(ProjectItem* item) const {
    QJsonObject obj;
    obj["name"] = item->name();
    obj["type"] = static_cast<int>(item->type());

    if (!item->path().isEmpty()) {
        obj["path"] = item->path();
    }

    if (item->childCount() > 0) {
        QJsonArray children;
        for (int i = 0; i < item->childCount(); ++i) {
            children.append(itemToJson(item->child(i)));
        }
        obj["children"] = children;
    }

    return obj;
}

void ProjectModel::fromJson(const QJsonObject& json) {
    beginResetModel();

    projectName_ = json["projectName"].toString("Untitled Project");
    setupDefaultStructure();
    rootItem_->setName(projectName_);

    // Restore files into categories
    QJsonArray categories = json["categories"].toArray();
    for (const QJsonValue& catVal : categories) {
        QJsonObject catObj = catVal.toObject();
        QString catName = catObj["name"].toString();
        ProjectItem* category = findCategory(catName);

        if (category && catObj.contains("children")) {
            QJsonArray children = catObj["children"].toArray();
            for (const QJsonValue& childVal : children) {
                QJsonObject childObj = childVal.toObject();
                QString fileName = childObj["name"].toString();
                QString filePath = childObj["path"].toString();

                auto fileType = static_cast<ProjectItem::ItemType>(childObj["type"].toInt());
                auto newFile = std::make_unique<ProjectItem>(fileName, fileType, category);
                newFile->setPath(filePath);
                category->appendChild(std::move(newFile));
            }
        }
    }

    endResetModel();
    emit projectChanged();
}

} // namespace widgets
} // namespace gui
} // namespace acutesim
