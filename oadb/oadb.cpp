#include "oadb/oadb.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace oadb {
namespace {

constexpr int kSchema = 2;

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
// Schema 2: cell properties and the schematic / symbol objects (points: packed little-endian int64 pairs)
const char* kTables2 =
    "CREATE TABLE cellprops(id INTEGER PRIMARY KEY,"
    " cell_id INTEGER NOT NULL REFERENCES cells(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,"
    " key TEXT NOT NULL, value TEXT NOT NULL, UNIQUE(cell_id, key));"
    "CREATE TABLE shapes(id INTEGER PRIMARY KEY,"
    " view_id INTEGER NOT NULL REFERENCES views(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,"
    " kind TEXT NOT NULL, layer TEXT NOT NULL, points BLOB NOT NULL, text TEXT NOT NULL);"
    "CREATE TABLE pins(id INTEGER PRIMARY KEY,"
    " view_id INTEGER NOT NULL REFERENCES views(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,"
    " name TEXT NOT NULL, direction TEXT NOT NULL, x INTEGER NOT NULL, y INTEGER NOT NULL, UNIQUE(view_id, name));"
    "CREATE TABLE instances(id INTEGER PRIMARY KEY,"
    " view_id INTEGER NOT NULL REFERENCES views(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,"
    " name TEXT NOT NULL, master_lib TEXT NOT NULL, master_cell TEXT NOT NULL, master_view TEXT NOT NULL,"
    " x INTEGER NOT NULL, y INTEGER NOT NULL, orient TEXT NOT NULL, UNIQUE(view_id, name));"
    "CREATE TABLE instparams(id INTEGER PRIMARY KEY,"
    " instance_id INTEGER NOT NULL REFERENCES instances(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,"
    " name TEXT NOT NULL, value TEXT NOT NULL, UNIQUE(instance_id, name));"
    "CREATE TABLE wires(id INTEGER PRIMARY KEY,"
    " view_id INTEGER NOT NULL REFERENCES views(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,"
    " points BLOB NOT NULL);"
    "CREATE TABLE labels(id INTEGER PRIMARY KEY,"
    " view_id INTEGER NOT NULL REFERENCES views(id) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,"
    " name TEXT NOT NULL, x INTEGER NOT NULL, y INTEGER NOT NULL);";
const std::vector<std::pair<std::string, std::vector<std::string>>> kLogged = {
    {"cells", {"name"}},
    {"views", {"cell_id", "name", "type"}},
    {"props", {"view_id", "key", "value"}},
    {"cellprops", {"cell_id", "key", "value"}},
    {"shapes", {"view_id", "kind", "layer", "points", "text"}},
    {"pins", {"view_id", "name", "direction", "x", "y"}},
    {"instances", {"view_id", "name", "master_lib", "master_cell", "master_view", "x", "y", "orient"}},
    {"instparams", {"instance_id", "name", "value"}},
    {"wires", {"view_id", "points"}},
    {"labels", {"view_id", "name", "x", "y"}},
};

std::string pack(const std::vector<Point>& pts) {
    std::string b;
    b.reserve(pts.size() * 16);
    for (const auto& p : pts)
        for (int64_t v : {p.x, p.y})
            for (int k = 0; k < 8; ++k) b.push_back((char)((uint64_t)v >> (8 * k) & 0xff));
    return b;
}

std::vector<Point> unpack(const void* data, int bytes) {
    const auto* d = (const unsigned char*)data;
    std::vector<Point> pts((size_t)bytes / 16);
    for (size_t i = 0; i < pts.size(); ++i)
        for (int c = 0; c < 2; ++c) {
            uint64_t v = 0;
            for (int k = 0; k < 8; ++k) v |= (uint64_t)d[i * 16 + c * 8 + k] << (8 * k);
            (c ? pts[i].y : pts[i].x) = (int64_t)v;
        }
    return pts;
}

class Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &s_, nullptr) != SQLITE_OK)
            throw Error(std::string("oadb: ") + sqlite3_errmsg(db) + " in: " + sql);
    }
    ~Stmt() { sqlite3_finalize(s_); }
    Stmt& bind(int i, const std::string& v) { sqlite3_bind_text(s_, i, v.c_str(), (int)v.size(), SQLITE_TRANSIENT); return *this; }
    Stmt& bind(int i, int64_t v) { sqlite3_bind_int64(s_, i, v); return *this; }
    Stmt& blob(int i, const std::string& v) { sqlite3_bind_blob(s_, i, v.data(), (int)v.size(), SQLITE_TRANSIENT); return *this; }
    std::vector<Point> points(int c) const { return unpack(sqlite3_column_blob(s_, c), sqlite3_column_bytes(s_, c)); }
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

bool validOrient(const std::string& o) {
    static const std::vector<std::string> all = {"R0", "R90", "R180", "R270", "MX", "MY", "MXR90", "MYR90"};
    return std::find(all.begin(), all.end(), o) != all.end();
}

bool validDirection(const std::string& d) { return d == "input" || d == "output" || d == "inputOutput"; }

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
        execOn(db, std::string("BEGIN;") + kTables + kTables2 + "COMMIT;");
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

std::string Library::viewType(const std::string& cell, const std::string& view) const {
    Stmt s(db_, "SELECT type FROM views WHERE id = ?1");
    s.bind(1, viewId(cell, view)).step();
    return s.text(0);
}

void Library::setCellProp(const std::string& cell, const std::string& key, const std::string& json) {
    need(!key.empty(), "oadb: a cell property needs a key");
    change("set " + cell + " " + key, [&] {
        Stmt(db_, "INSERT INTO cellprops(cell_id, key, value) VALUES(?1, ?2, ?3) "
                  "ON CONFLICT(cell_id, key) DO UPDATE SET value = excluded.value WHERE value IS NOT excluded.value")
            .bind(1, cellId(cell)).bind(2, key).bind(3, json).step();
    });
}

std::optional<std::string> Library::cellProp(const std::string& cell, const std::string& key) const {
    Stmt s(db_, "SELECT value FROM cellprops WHERE cell_id = ?1 AND key = ?2");
    if (!s.bind(1, cellId(cell)).bind(2, key).step()) return std::nullopt;
    return s.text(0);
}

namespace {
// What each view type may hold
void needIn(const std::string& type, std::initializer_list<const char*> allowed, const std::string& what) {
    for (const char* a : allowed) if (type == a) return;
    throw Error("oadb: a " + type + " view holds no " + what);
}
}  // namespace

int64_t Library::addShape(const std::string& cell, const std::string& view, Shape sh) {
    const size_t n = sh.points.size();
    const bool ok = (sh.kind == "line" && n >= 2) || ((sh.kind == "rect" || sh.kind == "ellipse") && n == 2) ||
                    (sh.kind == "polygon" && n >= 3) || (sh.kind == "label" && n == 1 && !sh.text.empty());
    need(ok, "oadb: a '" + sh.kind + "' shape with " + std::to_string(n) + " points is not valid "
             "(line >= 2, rect/ellipse 2 corners, polygon >= 3, label 1 point and text)");
    need(validName(sh.layer), "oadb: '" + sh.layer + "' is not a valid layer");
    int64_t id = 0;
    change("add " + sh.kind + " to " + cell + "/" + view, [&] {
        needIn(viewType(cell, view), {"symbol", "schematic"}, "shapes");
        Stmt(db_, "INSERT INTO shapes(view_id, kind, layer, points, text) VALUES(?1, ?2, ?3, ?4, ?5)")
            .bind(1, viewId(cell, view)).bind(2, sh.kind).bind(3, sh.layer).blob(4, pack(sh.points)).bind(5, sh.text).step();
        id = sqlite3_last_insert_rowid(db_);
    });
    return id;
}

int64_t Library::addPin(const std::string& cell, const std::string& view, Pin p) {
    need(validName(p.name), "oadb: '" + p.name + "' is not a valid pin name");
    need(validDirection(p.direction), "oadb: pin direction '" + p.direction + "' is not input, output or inputOutput");
    int64_t id = 0;
    change("add pin " + p.name + " to " + cell + "/" + view, [&] {
        needIn(viewType(cell, view), {"symbol", "schematic"}, "pins");
        const int64_t v = viewId(cell, view);
        Stmt q(db_, "SELECT 1 FROM pins WHERE view_id = ?1 AND name = ?2");
        need(!q.bind(1, v).bind(2, p.name).step(), "oadb: " + cell + "/" + view + " already has a pin " + p.name);
        Stmt(db_, "INSERT INTO pins(view_id, name, direction, x, y) VALUES(?1, ?2, ?3, ?4, ?5)")
            .bind(1, v).bind(2, p.name).bind(3, p.direction).bind(4, p.at.x).bind(5, p.at.y).step();
        id = sqlite3_last_insert_rowid(db_);
    });
    return id;
}

int64_t Library::addInstance(const std::string& cell, const std::string& view, Instance i) {
    need(validName(i.name), "oadb: '" + i.name + "' is not a valid instance name");
    need(validName(i.lib) && validName(i.cell) && validName(i.view), "oadb: instance " + i.name + " needs a master lib/cell/view");
    need(validOrient(i.orient), "oadb: orientation '" + i.orient + "' is not one of R0 R90 R180 R270 MX MY MXR90 MYR90");
    int64_t id = 0;
    change("add instance " + i.name + " to " + cell + "/" + view, [&] {
        needIn(viewType(cell, view), {"schematic"}, "instances");
        const int64_t v = viewId(cell, view);
        Stmt q(db_, "SELECT 1 FROM instances WHERE view_id = ?1 AND name = ?2");
        need(!q.bind(1, v).bind(2, i.name).step(), "oadb: " + cell + "/" + view + " already has an instance " + i.name);
        Stmt(db_, "INSERT INTO instances(view_id, name, master_lib, master_cell, master_view, x, y, orient) "
                  "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)")
            .bind(1, v).bind(2, i.name).bind(3, i.lib).bind(4, i.cell).bind(5, i.view).bind(6, i.at.x).bind(7, i.at.y)
            .bind(8, i.orient).step();
        id = sqlite3_last_insert_rowid(db_);
        for (const auto& [k, val] : i.params)
            Stmt(db_, "INSERT INTO instparams(instance_id, name, value) VALUES(?1, ?2, ?3)").bind(1, id).bind(2, k).bind(3, val).step();
    });
    return id;
}

int64_t Library::addWire(const std::string& cell, const std::string& view, Wire w) {
    need(w.points.size() >= 2, "oadb: a wire needs at least two points");
    for (size_t k = 1; k < w.points.size(); ++k)
        need(!(w.points[k] == w.points[k - 1]), "oadb: a wire's consecutive points must differ");
    int64_t id = 0;
    change("add wire to " + cell + "/" + view, [&] {
        needIn(viewType(cell, view), {"schematic"}, "wires");
        Stmt(db_, "INSERT INTO wires(view_id, points) VALUES(?1, ?2)").bind(1, viewId(cell, view)).blob(2, pack(w.points)).step();
        id = sqlite3_last_insert_rowid(db_);
    });
    return id;
}

int64_t Library::addLabel(const std::string& cell, const std::string& view, Label l) {
    need(validName(l.name), "oadb: '" + l.name + "' is not a valid net name");
    int64_t id = 0;
    change("add label " + l.name + " to " + cell + "/" + view, [&] {
        needIn(viewType(cell, view), {"schematic"}, "net labels");
        Stmt(db_, "INSERT INTO labels(view_id, name, x, y) VALUES(?1, ?2, ?3, ?4)")
            .bind(1, viewId(cell, view)).bind(2, l.name).bind(3, l.at.x).bind(4, l.at.y).step();
        id = sqlite3_last_insert_rowid(db_);
    });
    return id;
}

std::vector<Shape> Library::shapes(const std::string& cell, const std::string& view) const {
    std::vector<Shape> out;
    Stmt s(db_, "SELECT id, kind, layer, points, text FROM shapes WHERE view_id = ?1 ORDER BY id");
    s.bind(1, viewId(cell, view));
    while (s.step()) out.push_back({s.integer(0), s.text(1), s.text(2), s.points(3), s.text(4)});
    return out;
}

std::vector<Pin> Library::pins(const std::string& cell, const std::string& view) const {
    std::vector<Pin> out;
    Stmt s(db_, "SELECT id, name, direction, x, y FROM pins WHERE view_id = ?1 ORDER BY name");
    s.bind(1, viewId(cell, view));
    while (s.step()) out.push_back({s.integer(0), s.text(1), s.text(2), {s.integer(3), s.integer(4)}});
    return out;
}

std::vector<Instance> Library::instances(const std::string& cell, const std::string& view) const {
    std::vector<Instance> out;
    Stmt s(db_, "SELECT id, name, master_lib, master_cell, master_view, x, y, orient FROM instances WHERE view_id = ?1 ORDER BY name");
    s.bind(1, viewId(cell, view));
    while (s.step()) out.push_back({s.integer(0), s.text(1), s.text(2), s.text(3), s.text(4), {s.integer(5), s.integer(6)}, s.text(7), {}});
    for (auto& i : out) {
        Stmt p(db_, "SELECT name, value FROM instparams WHERE instance_id = ?1");
        p.bind(1, i.id);
        while (p.step()) i.params[p.text(0)] = p.text(1);
    }
    return out;
}

std::vector<Wire> Library::wires(const std::string& cell, const std::string& view) const {
    std::vector<Wire> out;
    Stmt s(db_, "SELECT id, points FROM wires WHERE view_id = ?1 ORDER BY id");
    s.bind(1, viewId(cell, view));
    while (s.step()) out.push_back({s.integer(0), s.points(1)});
    return out;
}

std::vector<Label> Library::labels(const std::string& cell, const std::string& view) const {
    std::vector<Label> out;
    Stmt s(db_, "SELECT id, name, x, y FROM labels WHERE view_id = ?1 ORDER BY id");
    s.bind(1, viewId(cell, view));
    while (s.step()) out.push_back({s.integer(0), s.text(1), {s.integer(2), s.integer(3)}});
    return out;
}

void Library::moveInstance(const std::string& cell, const std::string& view, const std::string& name, Point at,
                           const std::string& orient) {
    need(validOrient(orient), "oadb: orientation '" + orient + "' is not one of R0 R90 R180 R270 MX MY MXR90 MYR90");
    change("move " + name, [&] {
        Stmt u(db_, "UPDATE instances SET x = ?1, y = ?2, orient = ?3 WHERE view_id = ?4 AND name = ?5");
        u.bind(1, at.x).bind(2, at.y).bind(3, orient).bind(4, viewId(cell, view)).bind(5, name).step();
        need(sqlite3_changes(db_) == 1, "oadb: no instance " + name + " in " + cell + "/" + view);
    });
}

void Library::setInstanceParams(const std::string& cell, const std::string& view, const std::string& name,
                                const std::map<std::string, std::string>& params) {
    change("set parameters of " + name, [&] {
        Stmt q(db_, "SELECT id FROM instances WHERE view_id = ?1 AND name = ?2");
        need(q.bind(1, viewId(cell, view)).bind(2, name).step(), "oadb: no instance " + name + " in " + cell + "/" + view);
        const int64_t id = q.integer(0);
        for (const auto& [k, v] : params) {
            need(!k.empty(), "oadb: a parameter needs a name");
            if (v.empty())
                Stmt(db_, "DELETE FROM instparams WHERE instance_id = ?1 AND name = ?2").bind(1, id).bind(2, k).step();
            else
                Stmt(db_, "INSERT INTO instparams(instance_id, name, value) VALUES(?1, ?2, ?3) "
                          "ON CONFLICT(instance_id, name) DO UPDATE SET value = excluded.value WHERE value IS NOT excluded.value")
                    .bind(1, id).bind(2, k).bind(3, v).step();
        }
    });
}

void Library::deleteObject(const std::string& cell, const std::string& view, const std::string& kind, const std::string& key) {
    static const std::map<std::string, std::string> tables = {
        {"shape", "shapes"}, {"pin", "pins"}, {"instance", "instances"}, {"wire", "wires"}, {"label", "labels"}};
    const auto t = tables.find(kind);
    need(t != tables.end(), "oadb: no object kind '" + kind + "' (shape, pin, instance, wire, label)");
    change("delete " + kind + " " + key + " from " + cell + "/" + view, [&] {
        const bool byName = kind == "instance";
        Stmt d(db_, "DELETE FROM " + t->second + " WHERE view_id = ?1 AND " + (byName ? "name" : "id") + " = ?2");
        d.bind(1, viewId(cell, view));
        if (byName) d.bind(2, key);
        else {
            char* end = nullptr;
            const long long id = std::strtoll(key.c_str(), &end, 10);
            need(!key.empty() && *end == '\0', "oadb: a " + kind + " is deleted by its id, not '" + key + "'");
            d.bind(2, (int64_t)id);
        }
        d.step();
        need(sqlite3_changes(db_) == 1, "oadb: no " + kind + " " + key + " in " + cell + "/" + view);
    });
}

std::string Library::exportText() const {
    std::ostringstream o;
    o << "library " << name_ << "\n";
    auto pts = [](const std::vector<Point>& p) {
        std::string t;
        for (const auto& q : p) t += " (" + std::to_string(q.x) + " " + std::to_string(q.y) + ")";
        return t;
    };
    for (const auto& c : cells()) {
        o << "cell " << c << "\n";
        {
            Stmt s(db_, "SELECT key, value FROM cellprops WHERE cell_id = ?1 ORDER BY key");
            s.bind(1, cellId(c));
            while (s.step()) o << "  " << s.text(0) << " = " << s.text(1) << "\n";
        }
        for (const auto& v : views(c)) {
            o << "  view " << v.name << " " << v.type << "\n";
            for (const auto& [k, val] : props(c, v.name)) o << "    " << k << " = " << val << "\n";
            for (const auto& p : pins(c, v.name)) o << "    pin " << p.name << " " << p.direction << pts({p.at}) << "\n";
            for (const auto& i : instances(c, v.name)) {
                o << "    instance " << i.name << " " << i.lib << "/" << i.cell << "/" << i.view << pts({i.at}) << " " << i.orient;
                for (const auto& [k, val] : i.params) o << " " << k << "=" << val;
                o << "\n";
            }
            // shapes, wires and labels have no names: their geometry orders them, so ids never show
            std::vector<std::string> rows;
            for (const auto& sh : shapes(c, v.name)) rows.push_back("shape " + sh.kind + " " + sh.layer + pts(sh.points) + (sh.text.empty() ? "" : " \"" + sh.text + "\""));
            for (const auto& w : wires(c, v.name)) rows.push_back("wire" + pts(w.points));
            for (const auto& l : labels(c, v.name)) rows.push_back("label " + l.name + pts({l.at}));
            std::sort(rows.begin(), rows.end());
            for (const auto& r : rows) o << "    " << r << "\n";
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
        std::string word, name, path;
        if (!(ls >> word) || word[0] == '#') continue;
        ls >> name;
        std::getline(ls >> std::ws, path);  // the rest of the line: a path may hold spaces
        path.erase(path.find_last_not_of(" \t\r") + 1);
        if (word != "DEFINE" || path.empty() || !validName(name))
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
