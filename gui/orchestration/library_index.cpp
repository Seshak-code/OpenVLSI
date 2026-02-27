#include "library_index.h"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QDebug>

namespace acutesim::components {

// ============================================================================
// Cell
// ============================================================================
void Cell::addView(const QString& name, const QString& path) {
    views.append({name, path});
}

QJsonObject Cell::toJson() const {
    QJsonObject json;
    json["name"] = name;
    json["description"] = description;
    
    QJsonArray viewsArr;
    for (const auto& v : views) {
        QJsonObject vObj;
        vObj["name"] = v.name;
        vObj["file"] = v.filePath;
        viewsArr.append(vObj);
    }
    json["views"] = viewsArr;
    return json;
}

Cell Cell::fromJson(const QJsonObject& json) {
    Cell c;
    c.name = json["name"].toString();
    c.description = json["description"].toString();
    
    QJsonArray viewsArr = json["views"].toArray();
    for (const auto& vVal : viewsArr) {
        QJsonObject vObj = vVal.toObject();
        c.addView(vObj["name"].toString(), vObj["file"].toString());
    }
    return c;
}

// ============================================================================
// Library
// ============================================================================
void Library::addCell(const Cell& cell) {
    cells[cell.name] = cell;
}

QJsonObject Library::toJson() const {
    QJsonObject json;
    json["name"] = name;
    json["technology"] = technology;
    json["path"] = path;
    
    QJsonArray cellsArr;
    for (const auto& c : cells) {
        cellsArr.append(c.toJson());
    }
    json["cells"] = cellsArr;
    return json;
}

Library Library::fromJson(const QJsonObject& json) {
    Library lib;
    lib.name = json["name"].toString();
    lib.technology = json["technology"].toString();
    lib.path = json["path"].toString();
    
    QJsonArray cellsArr = json["cells"].toArray();
    for (const auto& cVal : cellsArr) {
        lib.addCell(Cell::fromJson(cVal.toObject()));
    }
    return lib;
}

void Library::scan() {
    QDir dir(path);
    if (!dir.exists()) return;
    
    // Look for subdirectories that represent cells
    // Heuristic: A directory is a cell if it contains .sch or .sym files
    // OR if it has a cell.json metadata file (future)
    
    QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        Cell cell;
        cell.name = entry.fileName();
        
        QDir cellDir(entry.absoluteFilePath());
        QFileInfoList files = cellDir.entryInfoList(QDir::Files);
        bool validCell = false;
        
        for (const auto& f : files) {
            QString ext = f.suffix().toLower();
            if (ext == "sch") {
                cell.addView("schematic", f.fileName());
                validCell = true;
            } else if (ext == "sym") {
                cell.addView("symbol", f.fileName());
                validCell = true;
            } else if (ext == "json" && f.fileName() == "layout.json") {
                cell.addView("layout", f.fileName());
                validCell = true;
            }
        }
        
        if (validCell) {
            addCell(cell);
        }
    }
    
    // Also check for "loose" files in the library root (simple structure)
    // e.g. inverter.sch in Lib root
    QFileInfoList files = dir.entryInfoList(QDir::Files);
    for (const auto& f : files) {
        if (f.suffix() == "sch") {
            QString base = f.completeBaseName();
            if (!cells.contains(base)) {
                Cell c;
                c.name = base;
                c.addView("schematic", f.fileName());
                // check for matching .sym
                if (dir.exists(base + ".sym")) c.addView("symbol", base + ".sym");
                addCell(c);
            }
        }
    }
}


// ============================================================================
// LibraryIndex
// ============================================================================
LibraryIndex& LibraryIndex::instance() {
    static LibraryIndex inst;
    return inst;
}

void LibraryIndex::addLibrary(const Library& lib) {
    libraries[lib.name] = lib;
}

Library* LibraryIndex::getLibrary(const QString& name) {
    if (libraries.contains(name)) return &libraries[name];
    return nullptr;
}

QVector<Library> LibraryIndex::getLibraries() const {
    QVector<Library> libs;
    for (auto c : libraries) libs.append(c);
    return libs;
}

void LibraryIndex::load(const QString& path) {
    indexFilePath = path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QJsonArray libsArr = root["libraries"].toArray();
    
    libraries.clear();
    for (const auto& val : libsArr) {
        addLibrary(Library::fromJson(val.toObject()));
    }
}

void LibraryIndex::save() {
    if (indexFilePath.isEmpty()) return;
    
    QJsonObject root;
    QJsonArray libsArr;
    for (const auto& lib : libraries) {
        libsArr.append(lib.toJson());
    }
    root["libraries"] = libsArr;
    
    QFile file(indexFilePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
    }
}

void LibraryIndex::scanProject(const QString& projectRoot) {
    // If index exists, load it first
    QString indexFile = projectRoot + "/library_index.json";
    if (QFile::exists(indexFile)) {
        load(indexFile);
        // Refresh scanned libraries?
        for (auto& lib : libraries) {
             // Re-scan to find new files
             lib.scan();
        }
    } else {
        // Create default library from project root
        Library defaultLib;
        defaultLib.name = "WorkLib";
        defaultLib.path = projectRoot;
        defaultLib.technology = "generic";
        defaultLib.scan();
        
        addLibrary(defaultLib);
        indexFilePath = indexFile;
        // Don't auto-save immediately, wait for user action? 
        // Or just keep in memory until save is requested.
    }
}

} // namespace acutesim::components
