#pragma once

#include <QAbstractItemModel>
#include <QIcon>
#include <QString>
#include <QJsonObject>
#include <memory>
#include <vector>

namespace acutesim {
namespace gui {
namespace widgets {

/**
 * ProjectItem - Node in the virtual project tree.
 * Represents folders (categories) or files (project assets).
 */
class ProjectItem {
public:
    enum class ItemType {
        Root,
        Folder,
        SchematicFile,
        HDLFile,
        ScriptFile,
        SynthesisFile,
        PhysicalFile,
        VerificationFile
    };

    explicit ProjectItem(const QString& name, ItemType type, ProjectItem* parent = nullptr);
    ~ProjectItem();

    void appendChild(std::unique_ptr<ProjectItem> child);
    ProjectItem* child(int row) const;
    int childCount() const;
    int row() const;
    ProjectItem* parent() const;

    QString name() const { return name_; }
    void setName(const QString& name) { name_ = name; }

    ItemType type() const { return type_; }

    QString path() const { return path_; }
    void setPath(const QString& path) { path_ = path; }

    QIcon icon() const;

private:
    QString name_;
    QString path_;
    ItemType type_;
    ProjectItem* parent_;
    std::vector<std::unique_ptr<ProjectItem>> children_;
};

/**
 * ProjectModel - Virtual project tree model.
 *
 * Provides a VS Code-style project explorer with predefined categories:
 * - Schematics/ (*.sch)
 * - HDL/ (*.v, *.sv, *.vhd)
 * - Scripts/ (*.tcl)
 * - Synthesis/ (*.blif, *.json - Yosys output)
 * - Physical Design/ (*.def, *.lef - OpenRoad output)
 * - Verification/ (*.vcd, test results)
 *
 * Data is stored in RAM by default (session persistence).
 * Call saveToJson()/loadFromJson() for explicit serialization.
 */
class ProjectModel : public QAbstractItemModel {
    Q_OBJECT

public:
    explicit ProjectModel(QObject* parent = nullptr);
    ~ProjectModel() override;

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Project operations
    void setProjectName(const QString& name);
    QString projectName() const;

    // File operations
    bool addFile(const QString& category, const QString& fileName, const QString& filePath = QString());
    bool removeFile(const QModelIndex& index);

    // Get item from index
    ProjectItem* itemFromIndex(const QModelIndex& index) const;

    // Serialization
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

    // Clear all files (keeps folder structure)
    void clear();

signals:
    void fileAdded(const QString& category, const QString& fileName);
    void fileRemoved(const QString& category, const QString& fileName);
    void projectChanged();

private:
    void setupDefaultStructure();
    ProjectItem* findCategory(const QString& categoryName) const;
    QJsonObject itemToJson(ProjectItem* item) const;
    void itemFromJson(ProjectItem* parent, const QJsonObject& json);

    std::unique_ptr<ProjectItem> rootItem_;
    QString projectName_;
};

} // namespace widgets
} // namespace gui
} // namespace acutesim
