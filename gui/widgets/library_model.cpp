#include "library_model.h"
#include "gui/core/icon_provider.h"
#include <QDebug>

namespace acutesim {
namespace gui {
namespace widgets {

// ============================================================================
// LibraryItem Helper Class
// ============================================================================
class LibraryItem {
public:
    enum class Type { Root, Library, Cell, View };

    LibraryItem(const QString& name, Type type, LibraryItem* parent = nullptr)
        : name_(name), type_(type), parent_(parent) {}

    void appendChild(std::unique_ptr<LibraryItem> child) {
        children_.push_back(std::move(child));
    }

    LibraryItem* child(int row) const {
        if (row < 0 || row >= static_cast<int>(children_.size())) return nullptr;
        return children_[row].get();
    }

    int childCount() const { return static_cast<int>(children_.size()); }
    
    int row() const {
        if (!parent_) return 0;
        for (size_t i = 0; i < parent_->children_.size(); ++i) {
            if (parent_->children_[i].get() == this) return static_cast<int>(i);
        }
        return 0;
    }

    LibraryItem* parent() const { return parent_; }
    QString name() const { return name_; }
    Type type() const { return type_; }
    
    QString path() const { return path_; }
    void setPath(const QString& p) { path_ = p; }

    QIcon icon() const {
        using namespace acutesim::gui::core;
        switch (type_) {
            case Type::Library: return IconProvider::icon("library"); // Need "library" icon, fallback to folder
            case Type::Cell: return IconProvider::icon("chip");    // Need "chip" icon, fallback to file
            case Type::View: 
                if (name_.contains("schematic", Qt::CaseInsensitive)) return IconProvider::icon("schematic");
                if (name_.contains("symbol", Qt::CaseInsensitive)) return IconProvider::icon("symbol");
                if (name_.contains("layout", Qt::CaseInsensitive)) return IconProvider::icon("layout");
                return IconProvider::icon("file");
            default: return IconProvider::icon("folder");
        }
    }

private:
    QString name_;
    Type type_;
    QString path_;
    LibraryItem* parent_;
    std::vector<std::unique_ptr<LibraryItem>> children_;
};

// ============================================================================
// LibraryModel Implementation
// ============================================================================

LibraryModel::LibraryModel(QObject* parent) 
    : QAbstractItemModel(parent) {
    refresh();
}

LibraryModel::~LibraryModel() = default;

void LibraryModel::refresh() {
    beginResetModel();
    rebuildTree();
    endResetModel();
}

void LibraryModel::rebuildTree() {
    rootItem_ = std::make_unique<LibraryItem>("Root", LibraryItem::Type::Root);
    
    auto& index = components::LibraryIndex::instance();
    QVector<components::Library> libs = index.getLibraries();
    
    // Sort libraries by name?
    // Using QMap in index implies alphabetical sort by key (name).
    
    for (const auto& lib : libs) {
        auto libItem = std::make_unique<LibraryItem>(lib.name, LibraryItem::Type::Library, rootItem_.get());
        libItem->setPath(lib.path);
        
        // Cells
        for (auto it = lib.cells.begin(); it != lib.cells.end(); ++it) {
            const auto& cell = it.value();
            auto cellItem = std::make_unique<LibraryItem>(cell.name, LibraryItem::Type::Cell, libItem.get());
            
            // Views
            for (const auto& view : cell.views) {
                auto viewItem = std::make_unique<LibraryItem>(view.name, LibraryItem::Type::View, cellItem.get());
                // Construct absolute path if needed
                QString fullPath = lib.path + "/" + cell.name + "/" + view.filePath;
                // Or if view.filePath is already relative to cell?
                // In scan(), we used fileName(), so likely just name.
                // Assuming Lib/Cell/View structure.
                viewItem->setPath(fullPath);
                cellItem->appendChild(std::move(viewItem));
            }
            libItem->appendChild(std::move(cellItem));
        }
        rootItem_->appendChild(std::move(libItem));
    }
}

QModelIndex LibraryModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent)) return QModelIndex();
    
    LibraryItem* parentItem = parent.isValid() 
        ? static_cast<LibraryItem*>(parent.internalPointer())
        : rootItem_.get();
        
    LibraryItem* childItem = parentItem->child(row);
    if (childItem) return createIndex(row, column, childItem);
    return QModelIndex();
}

QModelIndex LibraryModel::parent(const QModelIndex& index) const {
    if (!index.isValid()) return QModelIndex();
    
    LibraryItem* childItem = static_cast<LibraryItem*>(index.internalPointer());
    LibraryItem* parentItem = childItem->parent();
    
    if (parentItem == rootItem_.get() || !parentItem) return QModelIndex();
    return createIndex(parentItem->row(), 0, parentItem);
}

int LibraryModel::rowCount(const QModelIndex& parent) const {
    if (parent.column() > 0) return 0;
    
    LibraryItem* parentItem = parent.isValid()
        ? static_cast<LibraryItem*>(parent.internalPointer())
        : rootItem_.get();
        
    return parentItem->childCount();
}

int LibraryModel::columnCount(const QModelIndex&) const {
    return 1;
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return QVariant();
    
    LibraryItem* item = static_cast<LibraryItem*>(index.internalPointer());
    
    switch (role) {
        case Qt::DisplayRole: return item->name();
        case Qt::DecorationRole: return item->icon();
        case Qt::ToolTipRole: return item->path().isEmpty() ? item->name() : item->path();
        default: return QVariant();
    }
}

QString LibraryModel::getItemType(const QModelIndex& index) const {
    if (!index.isValid()) return "";
    LibraryItem* item = static_cast<LibraryItem*>(index.internalPointer());
    switch (item->type()) {
        case LibraryItem::Type::Library: return "Library";
        case LibraryItem::Type::Cell: return "Cell";
        case LibraryItem::Type::View: return "View";
        default: return "";
    }
}

QString LibraryModel::getItemPath(const QModelIndex& index) const {
    if (!index.isValid()) return "";
    LibraryItem* item = static_cast<LibraryItem*>(index.internalPointer());
    return item->path();
}

} // namespace widgets
} // namespace gui
} // namespace acutesim
