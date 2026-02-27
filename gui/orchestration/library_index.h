#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

namespace acutesim::components {

struct CellView {
    QString name;   // e.g. "schematic", "symbol", "layout"
    QString filePath; // Relative path or absolute
};

struct Cell {
    QString name;
    QVector<CellView> views;
    
    // Metadata
    QString description;
    
    void addView(const QString& name, const QString& path);
    QJsonObject toJson() const;
    static Cell fromJson(const QJsonObject& json);
};

struct Library {
    QString name;
    QString technology; // e.g. "sky130"
    QString path;       // Root directory of the library
    QMap<QString, Cell> cells;
    
    void addCell(const Cell& cell);
    QJsonObject toJson() const;
    static Library fromJson(const QJsonObject& json);
    
    // Auto-discovery
    void scan(); 
};

class LibraryIndex {
public:
    static LibraryIndex& instance();
    
    void load(const QString& indexFilePath);
    void save();
    
    // Management
    void addLibrary(const Library& lib);
    Library* getLibrary(const QString& name);
    QVector<Library> getLibraries() const;
    
    // Auto-discovery from project root
    void scanProject(const QString& projectRoot);
    
private:
    LibraryIndex() = default;
    QString indexFilePath;
    QMap<QString, Library> libraries;
};

} // namespace acutesim::components
