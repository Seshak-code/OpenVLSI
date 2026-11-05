#pragma once
// A layout view's geometry, resolved through its hierarchy: what the viewer draws and layout_info reports. Each master
// is tessellated once in its own coordinates; flattening then only transforms vertices.
#include "oadb/oadb.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace layout {

struct Box {
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    bool empty() const { return x0 > x1; }
    void add(double x, double y) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    }
    void add(const Box& b) { if (!b.empty()) { add(b.x0, b.y0); add(b.x1, b.y1); } }
};

using LayerKey = std::pair<int, int>;  // layer, datatype

// The transform of a reference (GDS's order: mirror about x, scale, rotate, translate)
struct Xform {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    void apply(double x, double y, double& ox, double& oy) const { ox = a * x + b * y + e; oy = c * x + d * y + f; }
    Xform then(const Xform& o) const;  // this first, then o
    static Xform of(const oadb::LayoutInstance& i, int col = 0, int row = 0);
};

struct FlatShape {  // one drawn shape, in top coordinates relative to Flat::origin
    float x0, y0, x1, y1;
    uint32_t layer;     // index into Flat::layers
    uint32_t tri, triCount;  // its triangle vertices in that layer's `tri`
    uint32_t master;    // index into Flat::masters
    int64_t object;     // what to edit: the top cell's shape id, or minus its instance's id for what an instance draws
};
struct FlatLayer {
    LayerKey key;
    std::vector<float> tri, line;  // x, y pairs: triangle vertices, line vertices
    uint64_t shapes = 0;
};
struct FlatLabel { float x, y; uint32_t layer; std::string text; int64_t object; };
struct Flat {
    double originX = 0, originY = 0;  // subtracted from every coordinate: floats keep their precision around the design
    Box box;                          // in database units, absolute
    std::vector<FlatLayer> layers;    // sorted by layer, datatype
    std::vector<FlatShape> shapes;
    std::vector<FlatLabel> labels;
    std::vector<std::string> masters;
    std::vector<std::pair<LayerKey, uint64_t>> ignored;
    uint64_t instances = 0;      // placed, arrays expanded
    uint64_t unfilled = 0;       // polygons left as outlines: self-intersecting beyond what ear clipping handles
    std::vector<float> boxes;    // x0 y0 x1 y1: masters past the level limit, drawn as boxes
};

class Hierarchy {
public:
    // lib/cell/view is the top. Masters without lib or view are in the same library, "layout" view. A missing master, or a
    // cycle, throws.
    Hierarchy(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view);
    ~Hierarchy();
    Flat flatten(int levels = 1 << 30) const;  // levels: how deep shapes are drawn; 0 is the top cell only
    Box box() const;
    uint64_t shapeCount() const;  // this cell's own, not through its instances
    std::vector<std::string> children() const;  // the masters this cell places directly
    double metersPerDbu() const;

private:
    struct Master;
    std::shared_ptr<Master> load(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view,
                                 std::vector<std::string>& stack);
    std::map<std::string, std::shared_ptr<Master>> masters_;
    std::shared_ptr<Master> top_;
};

// Tessellation, exposed for its test: triangles of a simple polygon (closing point optional); false when it could not
bool triangulate(const std::vector<oadb::Point>& polygon, std::vector<double>& xy);
// A path's outline as rectangles, one per segment plus a square at each inner vertex (x y quads, 8 values each)
std::vector<double> pathQuads(const std::vector<oadb::Point>& p, int64_t width, int pathType);

}  // namespace layout
