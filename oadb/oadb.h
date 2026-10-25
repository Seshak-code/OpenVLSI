#pragma once
// oadb: the design database (docs/rebuild.md). A library is one SQLite file holding cells, each with views (a
// schematic, a symbol, a layout, a test setup, ...); every view carries named properties whose values are JSON
// text. A workspace names the libraries a session sees (Cadence's cds.lib role).
//
// Every change runs in a transaction recorded as one undo group: undo() and redo() replay the inverse SQL that
// triggers logged (sqlite.org's undo/redo technique), so they need no SQLite extension and survive any schema change
// that keeps the triggers in step with the tables. The history lives for the session (a reopened library starts
// with none), as Cadence's does.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

struct sqlite3;

namespace oadb {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ViewInfo {
    std::string name, type;
};

class Library {
public:
    static std::unique_ptr<Library> create(const std::string& path, const std::string& name);
    static std::unique_ptr<Library> open(const std::string& path);
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    const std::string& name() const { return name_; }
    const std::string& path() const { return path_; }

    std::vector<std::string> cells() const;  // sorted
    void createCell(const std::string& cell);
    void renameCell(const std::string& from, const std::string& to);
    void deleteCell(const std::string& cell);  // with its views

    std::vector<ViewInfo> views(const std::string& cell) const;  // sorted by name
    void createView(const std::string& cell, const std::string& view, const std::string& type);
    void deleteView(const std::string& cell, const std::string& view);

    // JSON text per key; the caller owns its meaning (the command layer validates it)
    void setProp(const std::string& cell, const std::string& view, const std::string& key, const std::string& json);
    std::optional<std::string> prop(const std::string& cell, const std::string& view, const std::string& key) const;
    std::map<std::string, std::string> props(const std::string& cell, const std::string& view) const;

    // One undoable change: body's statements commit together or not at all (an exception rolls back and leaves no
    // undo step). A change inside a change joins the outer one.
    void change(const std::string& label, const std::function<void()>& body);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    std::string undoLabel() const { return undo_.empty() ? "" : undo_.back().label; }
    std::string redoLabel() const { return redo_.empty() ? "" : redo_.back().label; }
    bool undo();  // false: nothing to undo
    bool redo();

    // Deterministic text of the whole library (sorted), for diffs and review
    std::string exportText() const;

private:
    struct Group { std::string label; int64_t first, last; };
    Library(sqlite3* db, std::string path, std::string name);
    int64_t cellId(const std::string& cell) const;  // throws when missing
    int64_t viewId(const std::string& cell, const std::string& view) const;
    int64_t lastLogSeq() const;
    void exec(const std::string& sql) const;
    void replay(std::vector<Group>& from, std::vector<Group>& to);

    sqlite3* db_;
    std::string path_, name_;
    std::vector<Group> undo_, redo_;
    int depth_ = 0;  // change() nesting
};

// A workspace file: one "DEFINE <library> <path>" per line (relative paths against the file's directory)
class Workspace {
public:
    static Workspace load(const std::string& file);  // a missing file is an empty workspace
    void save() const;
    const std::string& file() const { return file_; }

    std::vector<std::string> libraries() const;  // sorted
    Library& library(const std::string& name);   // opened on first use; throws when undefined
    Library& createLibrary(const std::string& name, const std::string& path);
    void defineLibrary(const std::string& name, const std::string& path);  // an existing library file
    void undefineLibrary(const std::string& name);                         // the file stays

private:
    std::string file_;
    std::map<std::string, std::string> paths_;
    std::map<std::string, std::unique_ptr<Library>> open_;
    std::string resolve(const std::string& path) const;
};

// Names of libraries, cells and views: non-empty, no whitespace, no '/', at most 255 bytes
bool validName(const std::string& name);

}  // namespace oadb
