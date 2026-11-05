#include "layout/gds.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>

namespace layout {

namespace {
enum Rec : int {
    HEADER = 0x00, BGNLIB = 0x01, LIBNAME = 0x02, UNITS = 0x03, ENDLIB = 0x04, BGNSTR = 0x05, STRNAME = 0x06, ENDSTR = 0x07,
    BOUNDARY = 0x08, PATH = 0x09, SREF = 0x0A, AREF = 0x0B, TEXT = 0x0C, LAYER = 0x0D, DATATYPE = 0x0E, WIDTH = 0x0F, XY = 0x10,
    ENDEL = 0x11, SNAME = 0x12, COLROW = 0x13, NODE = 0x15, TEXTTYPE = 0x16, PRESENTATION = 0x17, STRING = 0x19, STRANS = 0x1A,
    MAG = 0x1B, ANGLE = 0x1C, REFLIBS = 0x1F, FONTS = 0x20, PATHTYPE = 0x21, GENERATIONS = 0x22, ATTRTABLE = 0x23, ELFLAGS = 0x26,
    PLEX = 0x2F, NODETYPE = 0x2A, PROPATTR = 0x2B, PROPVALUE = 0x2C, BOX = 0x2D, BOXTYPE = 0x2E, BGNEXTN = 0x30, ENDEXTN = 0x31,
};

int64_t beInt(const unsigned char* d, int n) {
    uint64_t v = 0;
    for (int k = 0; k < n; ++k) v = v << 8 | d[k];
    if (d[0] & 0x80) v |= ~uint64_t(0) << (8 * n);  // sign-extend
    return (int64_t)v;
}

double real8(const unsigned char* d) {
    const int exponent = (d[0] & 0x7f) - 64;
    uint64_t mantissa = 0;
    for (int k = 1; k < 8; ++k) mantissa = mantissa << 8 | d[k];
    const double v = std::ldexp((double)mantissa, 4 * exponent - 56);
    return d[0] & 0x80 ? -v : v;
}

void putReal8(std::string& out, double v) {
    unsigned char b[8] = {};
    if (v != 0.0) {
        const bool negative = v < 0;
        v = std::fabs(v);
        int exponent = 64;
        while (v >= 1.0) { v /= 16.0; ++exponent; }
        while (v < 1.0 / 16.0) { v *= 16.0; --exponent; }
        const uint64_t mantissa = (uint64_t)std::llround(std::ldexp(v, 56));
        b[0] = (unsigned char)((negative ? 0x80 : 0) | exponent);
        for (int k = 7; k >= 1; --k) b[k] = (unsigned char)(mantissa >> (8 * (7 - k)));
    }
    out.append((const char*)b, 8);
}

[[noreturn]] void fail(const std::string& path, size_t at, const std::string& what) {
    throw std::runtime_error("GDS " + path + " at offset " + std::to_string(at) + ": " + what);
}

class Writer {
public:
    explicit Writer(std::string& out) : out_(out) {}
    void rec(int type, int dtype, const std::string& body = {}) {
        if (body.size() > 65530) throw std::runtime_error("GDS: a record of " + std::to_string(body.size()) + " bytes does not fit");
        const size_t n = 4 + body.size();
        out_.push_back((char)(n >> 8));
        out_.push_back((char)n);
        out_.push_back((char)type);
        out_.push_back((char)dtype);
        out_ += body;
    }
    void i2(int type, std::initializer_list<int> vs) {
        std::string b;
        for (int v : vs) { b.push_back((char)(v >> 8)); b.push_back((char)v); }
        rec(type, 2, b);
    }
    void i4(int type, const std::vector<int64_t>& vs) {
        std::string b;
        for (int64_t v : vs) {
            if (v > std::numeric_limits<int32_t>::max() || v < std::numeric_limits<int32_t>::min())
                throw std::runtime_error("GDS: coordinate " + std::to_string(v) + " does not fit 32 bits");
            for (int k = 3; k >= 0; --k) b.push_back((char)((uint64_t)v >> (8 * k)));
        }
        rec(type, 3, b);
    }
    void text(int type, std::string s) {
        if (s.size() % 2) s.push_back('\0');
        rec(type, 6, s);
    }
    void xy(const std::vector<oadb::Point>& pts) {
        std::vector<int64_t> v;
        v.reserve(pts.size() * 2);
        for (const auto& p : pts) { v.push_back(p.x); v.push_back(p.y); }
        i4(XY, v);
    }

private:
    std::string& out_;
};
}  // namespace

GdsLibrary readGds(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    GdsLibrary lib;
    GdsCell* cell = nullptr;
    // the element being read
    int kind = -1, layer = 0, dtype = 0, pathType = 0, cols = 1, rows = 1;
    int64_t width = 0;
    bool mirror = false, sawHeader = false, sawEnd = false;
    double mag = 1.0, angle = 0.0;
    std::vector<oadb::Point> pts;
    std::string sname, str;

    size_t at = 0;
    const auto* d = (const unsigned char*)data.data();
    while (at < data.size() && !sawEnd) {
        if (at + 4 > data.size()) fail(path, at, "truncated record header");
        const size_t len = (size_t)d[at] << 8 | d[at + 1];
        const int type = d[at + 2];
        if (len < 4 || at + len > data.size()) fail(path, at, "truncated record");
        const unsigned char* b = d + at + 4;
        const size_t n = len - 4;
        auto string = [&] {
            std::string s((const char*)b, n);
            while (!s.empty() && s.back() == '\0') s.pop_back();
            return s;
        };
        auto need = [&](size_t bytes) { if (n < bytes) fail(path, at, "record " + std::to_string(type) + " is too short"); };
        const auto inCell = [&] { if (!cell) fail(path, at, "an element outside a structure"); };
        switch (type) {
            case HEADER: sawHeader = true; break;
            case BGNLIB: case REFLIBS: case FONTS: case GENERATIONS: case ATTRTABLE: case PRESENTATION: case ELFLAGS: case PLEX:
            case PROPATTR: case PROPVALUE: case BGNEXTN: case ENDEXTN: case NODETYPE: break;
            case LIBNAME: lib.name = string(); break;
            case UNITS:
                need(16);
                lib.userPerDbu = real8(b);
                lib.metersPerDbu = real8(b + 8);
                if (!(lib.metersPerDbu > 0)) fail(path, at, "a database unit must be positive");
                break;
            case ENDLIB: sawEnd = true; break;
            case BGNSTR:
                lib.cells.emplace_back();
                cell = &lib.cells.back();
                break;
            case STRNAME:
                inCell();
                cell->name = string();
                break;
            case ENDSTR: cell = nullptr; break;
            case BOUNDARY: case PATH: case SREF: case AREF: case TEXT: case BOX: case NODE:
                inCell();
                kind = type;
                layer = dtype = pathType = 0;
                cols = rows = 1;
                width = 0;
                mirror = false;
                mag = 1.0;
                angle = 0.0;
                pts.clear();
                sname.clear();
                str.clear();
                break;
            case LAYER: need(2); layer = (int)beInt(b, 2); break;
            case DATATYPE: case TEXTTYPE: case BOXTYPE: need(2); dtype = (int)beInt(b, 2); break;
            case WIDTH: need(4); width = std::llabs(beInt(b, 4)); break;
            case PATHTYPE: need(2); pathType = (int)beInt(b, 2); break;
            case XY:
                pts.resize(n / 8);
                for (size_t k = 0; k < pts.size(); ++k) pts[k] = {beInt(b + 8 * k, 4), beInt(b + 8 * k + 4, 4)};
                break;
            case SNAME: sname = string(); break;
            case COLROW: need(4); cols = (int)beInt(b, 2); rows = (int)beInt(b + 2, 2); break;
            case STRING: str = string(); break;
            case STRANS: {
                need(2);
                const int bits = b[0] << 8 | b[1];
                if (bits & 0x0006) fail(path, at, "absolute magnification or angle is not supported");
                mirror = bits & 0x8000;
                break;
            }
            case MAG: need(8); mag = real8(b); break;
            case ANGLE: need(8); angle = real8(b); break;
            case ENDEL: {
                inCell();
                if (kind == NODE) {
                    ++lib.nodesDropped;
                } else if (kind == SREF || kind == AREF) {
                    oadb::LayoutInstance i;
                    i.cell = sname;
                    i.angle = angle;
                    i.mirrorX = mirror;
                    i.mag = mag;
                    if (sname.empty() || pts.size() != (kind == SREF ? 1u : 3u)) fail(path, at, "a reference without its name or points");
                    i.at = pts[0];
                    if (kind == AREF) {
                        if (cols < 1 || rows < 1) fail(path, at, "an array of no columns or rows");
                        i.cols = cols;
                        i.rows = rows;
                        i.colStep = {(pts[1].x - pts[0].x) / cols, (pts[1].y - pts[0].y) / cols};
                        i.rowStep = {(pts[2].x - pts[0].x) / rows, (pts[2].y - pts[0].y) / rows};
                    }
                    cell->instances.push_back(std::move(i));
                } else if (kind == BOUNDARY || kind == BOX || kind == PATH || kind == TEXT) {
                    if (pts.empty()) fail(path, at, "an element without points");
                    oadb::LayoutShape s;
                    s.layer = layer;
                    s.datatype = dtype;
                    s.kind = kind == PATH ? "path" : kind == TEXT ? "text" : "polygon";
                    s.points = std::move(pts);
                    s.width = width;
                    s.pathType = pathType;
                    s.text = str;
                    cell->shapes.push_back(std::move(s));
                    pts = {};
                } else {
                    fail(path, at, "ENDEL without an element");
                }
                kind = -1;
                break;
            }
            default:
                fail(path, at, "unknown record type " + std::to_string(type));
        }
        at += len;
    }
    if (!sawHeader) fail(path, 0, "not a GDS file");
    if (!sawEnd) fail(path, at, "no ENDLIB: the file is truncated");
    for (const auto& c : lib.cells)
        if (c.name.empty()) throw std::runtime_error("GDS " + path + ": a structure without a name");
    return lib;
}

void writeGds(const std::string& path, const GdsLibrary& lib) {
    std::string out;
    Writer w(out);
    const std::initializer_list<int> stamp = {2026, 1, 1, 0, 0, 0, 2026, 1, 1, 0, 0, 0};  // fixed: identical input, identical file
    w.i2(HEADER, {600});
    w.i2(BGNLIB, stamp);
    w.text(LIBNAME, lib.name);
    {
        std::string u;
        putReal8(u, lib.userPerDbu);
        putReal8(u, lib.metersPerDbu);
        w.rec(UNITS, 5, u);
    }
    for (const auto& c : lib.cells) {
        w.i2(BGNSTR, stamp);
        w.text(STRNAME, c.name);
        for (const auto& s : c.shapes) {
            if (s.kind == "polygon") {
                w.rec(BOUNDARY, 0);
                w.i2(LAYER, {s.layer});
                w.i2(DATATYPE, {s.datatype});
                w.xy(s.points);
            } else if (s.kind == "path") {
                w.rec(PATH, 0);
                w.i2(LAYER, {s.layer});
                w.i2(DATATYPE, {s.datatype});
                w.i2(PATHTYPE, {s.pathType});
                w.i4(WIDTH, {s.width});
                w.xy(s.points);
            } else if (s.kind == "text") {
                w.rec(TEXT, 0);
                w.i2(LAYER, {s.layer});
                w.i2(TEXTTYPE, {s.datatype});
                w.xy(s.points);
                w.text(STRING, s.text);
            } else {
                throw std::runtime_error("GDS: cannot write a shape of kind '" + s.kind + "'");
            }
            w.rec(ENDEL, 0);
        }
        for (const auto& i : c.instances) {
            const bool array = i.cols > 1 || i.rows > 1;
            w.rec(array ? AREF : SREF, 0);
            w.text(SNAME, i.cell);
            if (i.mirrorX || i.mag != 1.0 || i.angle != 0.0) {
                w.rec(STRANS, 1, std::string(i.mirrorX ? "\x80\0" : "\0\0", 2));  // a bit array, by its data type
                if (i.mag != 1.0) { std::string b; putReal8(b, i.mag); w.rec(MAG, 5, b); }
                if (i.angle != 0.0) { std::string b; putReal8(b, i.angle); w.rec(ANGLE, 5, b); }
            }
            if (array) {
                w.i2(COLROW, {i.cols, i.rows});
                w.xy({i.at, {i.at.x + i.colStep.x * i.cols, i.at.y + i.colStep.y * i.cols},
                      {i.at.x + i.rowStep.x * i.rows, i.at.y + i.rowStep.y * i.rows}});
            } else {
                w.xy({i.at});
            }
            w.rec(ENDEL, 0);
        }
        w.rec(ENDSTR, 0);
    }
    w.rec(ENDLIB, 0);
    std::ofstream f(path, std::ios::binary);
    f.write(out.data(), (std::streamsize)out.size());
    if (!f) throw std::runtime_error("cannot write " + path);
}

}  // namespace layout
