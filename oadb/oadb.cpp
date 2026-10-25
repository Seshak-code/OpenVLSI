#include "oadb/oadb.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace oadb {
namespace {

constexpr int kSchema = 1;

// The tables, and for each its non-key columns: what the undo triggers restore
const char* kTables =
    "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
    "CREATE TABLE cells(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);"
    // Deferred keys: an undo replays a change backwards, possibly a child before its parent
    "CREATE TABLE views(id INTEGER PRIMARY KEY,"
    " cell_id INTEGER NOT NULL REFERENCES cells(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,"
    " name TEXT NOT NULL, type TEXT NOT NULL, UNIQUE(cell_id, name));"
    "CREATE TABLE props(id INTEGER PRIMARY KEY,"
    " view_id INTEGER NOT NULL REFERENCES views(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,"
    " key TEXT NOT NULL, value TEXT NOT NULL, UNIQUE(view_id, key));";
const std::vector<std::pair<std::string, std::vector<std::string>>> kLogged = {
    {"cells", {"name"}},
    {"views", {"cell_id", "name", "type"}},
    {"props", {"view_id", "key", "value"}},
};

class Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &s_, nullptr) != SQLITE_OK)
            throw Error(std::string("oadb: ") + sqlite3_errmsg(db) + " in: " + sql);
    }
    ~Stmt() { sqlite3_finalize(s_); }
    Stmt& bind(int i, const std::string& v) { sqlite3_bind_text(s_, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT); return *this; }
    Stmt& bind(int i, int64_t v) { sqlite3_bind_int64(s_, i, v); return *this; }
    bool step() {  // true: a row
        const int rc = sqlite3_step(s_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw Error(std::string("oadb: ") + sqlite3_errmsg(db_));
    }
    std::string text(int c) const { const auto* t = sqlite3_column_text(s_, c); return t ? (const char*)t : ""; }
    int64_t integer(int c) const { return sqlite3_column_int64(s_, c); }

private:
    sqlite3* db_;
    sqlite3_stmt* s_ = nullptr;
};

sqlite3* openFile(const std::string& path, int flags) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, flags, nullptr) != SQLITE_OK) {
        const std::string e = db ? sqlite3_errmsg(db) : "out of memory";
        sqlite3_close(db);
        throw Error("oadb: cannot open " + path + ": " + e);
    }
    sqlite3_busy_timeout(db, 5000);  // another process (the GUI, the MCP server) holding the write lock briefly
    return db;
}

void execOn(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string e = err ? err : "unknown error";
        sqlite3_free(err);
        throw Error("oadb: " + e);
    }
}

// Session-scoped (TEMP) undo log and triggers: each change to a logged table records the SQL that reverses it
void installUndo(sqlite3* db) {
    std::string sql = "CREATE TEMP TABLE undolog(seq INTEGER PRIMARY KEY, sql TEXT NOT NULL);";
    for (const auto& [t, cols] : kLogged) {
        std::string set, names = "rowid", values = "'||old.rowid||'";
        for (size_t i = 0; i < cols.size(); ++i) {
            set += (i ? "," : "") + cols[i] + "='||quote(old." + cols[i] + ")||'";
            names += "," + cols[i];
            values += ",'||quote(old." + cols[i] + ")||'";
        }
        sql += "CREATE TEMP TRIGGER u_" + t + "_i AFTER INSERT ON main." + t +
               " BEGIN INSERT INTO undolog(sql) VALUES('DELETE FROM main." + t + " WHERE rowid='||new.rowid); END;";
        sql += "CREATE TEMP TRIGGER u_" + t + "_u AFTER UPDATE ON main." + t +
               " BEGIN INSERT INTO undolog(sql) VALUES('UPDATE main." + t + " SET " + set +
               " WHERE rowid='||old.rowid); END;";
        sql += "CREATE TEMP TRIGGER u_" + t + "_d AFTER DELETE ON main." + t +
               " BEGIN INSERT INTO undolog(sql) VALUES('INSERT INTO main." + t + "(" + names + ") VALUES(" + values +
               ")'); END;";
    }
    execOn(db, sql);
}

void need(bool ok, const std::string& what) {
    if (!ok) throw Error(what);
}

}  // namespace

bool validName(const std::string& name) {
    if (name.empty() || name.size() > 255) return false;
    return std::none_of(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c) || c == '/' || c < 0x20; });
}

Library::Library(sqlite3* db, std::string path, std::string name) : db_(db), path_(std::move(path)), name_(std::move(name)) {
    execOn(db_, "PRAGMA foreign_keys=ON");
    installUndo(db_);
}

Library::~Library() { sqlite3_close(db_); }

std::unique_ptr<Library> Library::create(const std::string& path, const std::string& name) {
    need(validName(name), "oadb: '" + name + "' is not a valid library name");
    need(!std::filesystem::exists(path), "oadb: " + path + " already exists");
    sqlite3* db = openFile(path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    try {
        execOn(db, "PRAGMA journal_mode=WAL");  // readers in other processes while one writes
        execOn(db, std::string("BEGIN;") + kTables + "COMMIT;");
        Stmt s(db, "INSERT INTO meta(key, value) VALUES('schema', ?1), ('name', ?2)");
        s.bind(1, std::to_string(kSchema)).bind(2, name).step();
    } catch (...) {
        sqlite3_close(db);
        std::filesystem::remove(path);
        throw;
    }
    return std::unique_ptr<Library>(new Library(db, path, name));
}

std::unique_ptr<Library> Library::open(const std::string& path) {
    need(std::filesystem::exists(path), "oadb: no library at " + path);
    sqlite3* db = openFile(path, SQLITE_OPEN_READWRITE);
    std::string schema, name;
    try {
        Stmt s(db, "SELECT key, value FROM meta WHERE key IN ('schema', 'name')");
        while (s.step()) (s.text(0) == "schema" ? schema : name) = s.text(1);
    } catch (const Error&) {
        sqlite3_close(db);
        throw Error("oadb: " + path + " is not an OpenVLSI library");
    }
    if (schema != std::to_string(kSchema) || name.empty()) {
        sqlite3_close(db);
        throw Error("oadb: " + path + " has library schema '" + schema + "', this build reads " + std::to_string(kSchema));
    }
    return std::unique_ptr<Library>(new Library(db, path, name));
}

void Library::exec(const std::string& sql) const { execOn(db_, sql); }

int64_t Library::lastLogSeq() const {
    Stmt s(db_, "SELECT coalesce(max(seq), 0) FROM undolog");
    s.step();
    return s.integer(0);
}

int64_t Library::cellId(const std::string& cell) const {
    Stmt s(db_, "SELECT id FROM cells WHERE name = ?1");
    if (!s.bind(1, cell).step()) throw Error("oadb: no cell '" + cell + "' in library " + name_);
    return s.integer(0);
}

int64_t Library::viewId(const std::string& cell, const std::string& view) const {
    Stmt s(db_, "SELECT v.id FROM views v JOIN cells c ON c.id = v.cell_id WHERE c.name = ?1 AND v.name = ?2");
    if (!s.bind(1, cell).bind(2, view).step()) throw Error("oadb: no view '" + cell + "/" + view + "' in library " + name_);
    return s.integer(0);
}

void Library::change(const std::string& label, const std::function<void()>& body) {
    if (depth_ > 0) { body(); return; }
    exec("BEGIN IMMEDIATE");
    const int64_t before = lastLogSeq();
    ++depth_;
    try {
        body();
        exec("COMMIT");  // the deferred foreign keys are checked here
    } catch (...) {
        --depth_;
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
    --depth_;
    const int64_t after = lastLogSeq();
    if (after == before) return;  // nothing changed: no undo step
    for (const auto& g : redo_)  // a new change ends the redo history
        exec("DELETE FROM undolog WHERE seq BETWEEN " + std::to_string(g.first) + " AND " + std::to_string(g.last));
    redo_.clear();
    undo_.push_back({label, before + 1, after});
}

void Library::replay(std::vector<Group>& from, std::vector<Group>& to) {
    const Group g = from.back();
    exec("BEGIN IMMEDIATE");
    try {
        std::vector<std::string> sql;
        {
            Stmt s(db_, "SELECT sql FROM undolog WHERE seq BETWEEN ?1 AND ?2 ORDER BY seq DESC");
            s.bind(1, g.first).bind(2, g.last);
            while (s.step()) sql.push_back(s.text(0));
        }
        exec("DELETE FROM undolog WHERE seq BETWEEN " + std::to_string(g.first) + " AND " + std::to_string(g.last));
        const int64_t before = lastLogSeq();
        for (const auto& q : sql) exec(q);  // each logs its own inverse: the step back
        const int64_t after = lastLogSeq();
        exec("COMMIT");
        from.pop_back();
        to.push_back({g.label, before + 1, after});
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

bool Library::undo() {
    if (undo_.empty()) return false;
    replay(undo_, redo_);
    return true;
}

bool Library::redo() {
    if (redo_.empty()) return false;
    replay(redo_, undo_);
    return true;
}

std::vector<std::string> Library::cells() const {
    std::vector<std::string> out;
    Stmt s(db_, "SELECT name FROM cells ORDER BY name");
    while (s.step()) out.push_back(s.text(0));
    return out;
}

void Library::createCell(const std::string& cell) {
    need(validName(cell), "oadb: '" + cell + "' is not a valid cell name");
    change("create cell " + cell, [&] {
        Stmt q(db_, "SELECT 1 FROM cells WHERE name = ?1");
        need(!q.bind(1, cell).step(), "oadb: cell '" + cell + "' already exists in library " + name_);
        Stmt(db_, "INSERT INTO cells(name) VALUES(?1)").bind(1, cell).step();
    });
}

void Library::renameCell(const std::string& from, const std::string& to) {
    need(validName(to), "oadb: '" + to + "' is not a valid cell name");
    change("rename cell " + from + " to " + to, [&] {
        const int64_t id = cellId(from);
        Stmt q(db_, "SELECT 1 FROM cells WHERE name = ?1");
        need(!q.bind(1, to).step(), "oadb: cell '" + to + "' already exists in library " + name_);
        Stmt(db_, "UPDATE cells SET name = ?1 WHERE id = ?2").bind(1, to).bind(2, id).step();
    });
}

void Library::deleteCell(const std::string& cell) {
    change("delete cell " + cell, [&] { Stmt(db_, "DELETE FROM cells WHERE id = ?1").bind(1, cellId(cell)).step(); });
}

std::vector<ViewInfo> Library::views(const std::string& cell) const {
    std::vector<ViewInfo> out;
    Stmt s(db_, "SELECT name, type FROM views WHERE cell_id = ?1 ORDER BY name");
    s.bind(1, cellId(cell));
    while (s.step()) out.push_back({s.text(0), s.text(1)});
    return out;
}

void Library::createView(const std::string& cell, const std::string& view, const std::string& type) {
    need(validName(view), "oadb: '" + view + "' is not a valid view name");
    need(validName(type), "oadb: '" + type + "' is not a valid view type");
    change("create view " + cell + "/" + view, [&] {
        const int64_t c = cellId(cell);
        Stmt q(db_, "SELECT 1 FROM views WHERE cell_id = ?1 AND name = ?2");
        need(!q.bind(1, c).bind(2, view).step(), "oadb: view '" + cell + "/" + view + "' already exists in library " + name_);
        Stmt(db_, "INSERT INTO views(cell_id, name, type) VALUES(?1, ?2, ?3)").bind(1, c).bind(2, view).bind(3, type).step();
    });
}

void Library::deleteView(const std::string& cell, const std::string& view) {
    change("delete view " + cell + "/" + view,
           [&] { Stmt(db_, "DELETE FROM views WHERE id = ?1").bind(1, viewId(cell, view)).step(); });
}

void Library::setProp(const std::string& cell, const std::string& view, const std::string& key, const std::string& json) {
    need(!key.empty(), "oadb: a property needs a key");
    change("set " + cell + "/" + view + " " + key, [&] {
        Stmt(db_, "INSERT INTO props(view_id, key, value) VALUES(?1, ?2, ?3) "
                  "ON CONFLICT(view_id, key) DO UPDATE SET value = excluded.value WHERE value IS NOT excluded.value")
            .bind(1, viewId(cell, view)).bind(2, key).bind(3, json).step();
    });
}

std::optional<std::string> Library::prop(const std::string& cell, const std::string& view, const std::string& key) const {
    Stmt s(db_, "SELECT value FROM props WHERE view_id = ?1 AND key = ?2");
    if (!s.bind(1, viewId(cell, view)).bind(2, key).step()) return std::nullopt;
    return s.text(0);
}

std::map<std::string, std::string> Library::props(const std::string& cell, const std::string& view) const {
    std::map<std::string, std::string> out;
    Stmt s(db_, "SELECT key, value FROM props WHERE view_id = ?1");
    s.bind(1, viewId(cell, view));
    while (s.step()) out[s.text(0)] = s.text(1);
    return out;
}

std::string Library::exportText() const {
    std::ostringstream o;
    o << "library " << name_ << "\n";
    for (const auto& c : cells()) {
        o << "cell " << c << "\n";
        for (const auto& v : views(c)) {
            o << "  view " << v.name << " " << v.type << "\n";
            for (const auto& [k, val] : props(c, v.name)) o << "    " << k << " = " << val << "\n";
        }
    }
    return o.str();
}

// ── Workspace ────────────────────────────────────────────────────────────────────────────────────────────────────

Workspace Workspace::load(const std::string& file) {
    Workspace w;
    w.file_ = file;
    std::ifstream in(file);
    if (!in) return w;
    std::string line;
    for (int n = 1; std::getline(in, line); ++n) {
        std::istringstream ls(line);
        std::string word, name, path, rest;
        if (!(ls >> word) || word[0] == '#') continue;
        if (word != "DEFINE" || !(ls >> name >> path) || (ls >> rest) || !validName(name))
            throw Error("oadb: " + file + ":" + std::to_string(n) + ": expected 'DEFINE <library> <path>'");
        if (!w.paths_.emplace(name, path).second)
            throw Error("oadb: " + file + ":" + std::to_string(n) + ": library " + name + " defined twice");
    }
    return w;
}

void Workspace::save() const {
    std::ofstream out(file_);
    if (!out) throw Error("oadb: cannot write " + file_);
    for (const auto& [name, path] : paths_) out << "DEFINE " << name << " " << path << "\n";
    if (!out.flush()) throw Error("oadb: cannot write " + file_);
}

std::string Workspace::resolve(const std::string& path) const {
    const std::filesystem::path p(path);
    return p.is_absolute() ? path : (std::filesystem::path(file_).parent_path() / p).string();
}

std::vector<std::string> Workspace::libraries() const {
    std::vector<std::string> out;
    for (const auto& [name, path] : paths_) out.push_back(name);
    return out;
}

Library& Workspace::library(const std::string& name) {
    if (auto it = open_.find(name); it != open_.end()) return *it->second;
    const auto p = paths_.find(name);
    need(p != paths_.end(), "oadb: no library '" + name + "' in workspace " + file_);
    auto lib = Library::open(resolve(p->second));
    need(lib->name() == name, "oadb: " + p->second + " holds library '" + lib->name() + "', not '" + name + "'");
    return *(open_[name] = std::move(lib));
}

Library& Workspace::createLibrary(const std::string& name, const std::string& path) {
    need(!paths_.count(name), "oadb: library '" + name + "' is already defined");
    auto lib = Library::create(resolve(path), name);
    paths_[name] = path;
    save();
    return *(open_[name] = std::move(lib));
}

void Workspace::defineLibrary(const std::string& name, const std::string& path) {
    need(!paths_.count(name), "oadb: library '" + name + "' is already defined");
    auto lib = Library::open(resolve(path));
    need(lib->name() == name, "oadb: " + path + " holds library '" + lib->name() + "', not '" + name + "'");
    paths_[name] = path;
    save();
    open_[name] = std::move(lib);
}

void Workspace::undefineLibrary(const std::string& name) {
    need(paths_.erase(name) == 1, "oadb: no library '" + name + "' in workspace " + file_);
    open_.erase(name);
    save();
}

}  // namespace oadb
