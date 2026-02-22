#pragma once

#include <QAbstractItemModel>
#include <QIcon>
#include <QString>
#include <vector>
#include <memory>
#include "components/library_index.h"

namespace acutesim {
namespace gui {
namespace widgets {

// Forward decl
class LibraryItem;

/**
 * LibraryModel - Tree model for Library Index (Logical View).
 * Hierarchy: Library -> Cell -> View
 */
class LibraryModel : public QAbstractItemModel {
    Q_OBJECT

public:
    explicit LibraryModel(QObject* parent = nullptr);
    ~LibraryModel() override;

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    
    // Refresh from LibraryIndex
    void refresh();

    // Helper to get raw data
    // Returns type: "Library", "Cell", "View"
    QString getItemType(const QModelIndex& index) const;
    QString getItemPath(const QModelIndex& index) const; // For opening files

private:
    void rebuildTree();
    
    std::unique_ptr<LibraryItem> rootItem_;
};

} // namespace widgets
} // namespace gui
} // namespace acutesim
