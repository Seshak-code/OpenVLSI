#include "layout/pcell.h"

#include <algorithm>
#include <stdexcept>

namespace layout {

oadb::LayoutShape rectShape(const LayerKey& layer, const Rect& r) {
    oadb::LayoutShape s;
    s.layer = layer.first;
    s.datatype = layer.second;
    s.kind = "polygon";
    s.points = {{r.x0, r.y0}, {r.x1, r.y0}, {r.x1, r.y1}, {r.x0, r.y1}, {r.x0, r.y0}};
    return s;
}

namespace {

// SG13G2 front end, from sg13g2_maximal.lydrc
const LayerKey kActiv{1, 0}, kGatPoly{5, 0}, kCont{6, 0}, kMetal1{8, 0}, kPsd{14, 0}, kNwell{31, 0};
constexpr int64_t kContSize = 160;      // Cnt.a
constexpr int64_t kContSpace = 180;     // Cnt.b
constexpr int64_t kActivOverCont = 70;  // Cnt.c
constexpr int64_t kContToGate = 110;    // Cnt.f
constexpr int64_t kGateEndcap = 180;    // Gat.c
constexpr int64_t kPolyOverCont = 70;   // Cnt.d
constexpr int64_t kContOnPolyToActiv = 140;  // Cnt.e
constexpr int64_t kPsdOverGate = 300;   // pSD.i
constexpr int64_t kMetalOverCont = 50;  // M1.c1, used on every side: a strip's ends are its endcaps
constexpr int64_t kPsdOverActiv = 180;  // pSD.c
constexpr int64_t kNwellOverActiv = 310;  // NW.c
constexpr int64_t kMetalSpace = 180;    // M1.b
constexpr int64_t kMetalArea = 90000;   // M1.d
// Poly resistors
const LayerKey kNsd{7, 0}, kRes{24, 0}, kSalBlock{28, 0}, kExtBlock{111, 0}, kPolyRes{128, 0};
constexpr int64_t kResWidth = 500;       // Rsil.a, Rppd.a, Rhi.a
constexpr int64_t kResLength = 500;      // Rsil.f, Rppd.e, Rhi.f
constexpr int64_t kResToCont = 120;      // Rsil.b
constexpr int64_t kSalToCont = 200;      // Rppd.c, Rhi.d: exactly
constexpr int64_t kSalOverPoly = 200;    // Sal.c
constexpr int64_t kImplantOverPoly = 180;  // Rsil.e, Rppd.b, Rppd.d, Rhi.c, Rhi.e

int64_t floorToGrid(int64_t v, int64_t grid) { return (v >= 0 ? v : v - grid + 1) / grid * grid; }
int64_t ceilToGrid(int64_t v, int64_t grid) { return floorToGrid(v + grid - 1, grid); }

}  // namespace

std::vector<oadb::LayoutShape> viaShapes(const Tech& t, int via, int64_t x, int64_t y, int cols, int rows) {
    if (via < 0 || via >= (int)t.vias.size()) throw std::invalid_argument("no such via in " + t.name);
    if (cols < 1 || rows < 1) throw std::invalid_argument("a via needs at least one cut each way");
    const TechVia& v = t.vias[via];
    const int64_t w = cols * v.cut + (cols - 1) * v.space, h = rows * v.cut + (rows - 1) * v.space;
    const int64_t x0 = floorToGrid(x - w / 2, t.grid), y0 = floorToGrid(y - h / 2, t.grid);
    std::vector<oadb::LayoutShape> out;
    for (int c = 0; c < cols; ++c)
        for (int r = 0; r < rows; ++r) {
            const int64_t cx = x0 + c * (v.cut + v.space), cy = y0 + r * (v.cut + v.space);
            out.push_back(rectShape({v.cutLayer, 0}, {cx, cy, cx + v.cut, cy + v.cut}));
        }
    // The lower metal's endcaps along x, the upper's along y: as routes usually run on alternating directions. Grown to
    // the metal's minimum width, not its minimum area: a via lands on a wire, and the wire carries the area.
    const auto metal = [&](int m, int64_t ex, int64_t ey) {
        const TechMetal& tm = t.metals[m];
        const int64_t gx = ceilToGrid(std::max<int64_t>(0, tm.minWidth - w - 2 * ex) / 2, t.grid);
        const int64_t gy = ceilToGrid(std::max<int64_t>(0, tm.minWidth - h - 2 * ey) / 2, t.grid);
        out.push_back(rectShape({tm.layer, 0}, {x0 - ex - gx, y0 - ey - gy, x0 + w + ex + gx, y0 + h + ey + gy}));
    };
    metal(v.lower, v.endcapLower, v.enclosureLower);
    metal(v.upper, v.enclosureUpper, v.endcapUpper);
    return out;
}

Generated mosCell(const Tech& t, bool pmos, int64_t w, int64_t l, int ng) {
    if (t.name != "sg13g2") throw std::invalid_argument("no transistor generator for process " + t.name);
    if (ng < 1) throw std::invalid_argument("a transistor needs at least one finger");
    if (l < 130) throw std::invalid_argument("gate length " + std::to_string(l) + " nm is below the process's 130 nm (Gat.a)");
    if (w % ng) throw std::invalid_argument("width " + std::to_string(w) + " nm does not divide over " + std::to_string(ng) + " fingers");
    const int64_t wf = w / ng;
    if (wf % t.grid || l % t.grid) throw std::invalid_argument("width per finger and length must be on the " + std::to_string(t.grid) + " nm grid");
    if (wf < kContSize + 2 * kActivOverCont) throw std::invalid_argument("a finger of " + std::to_string(wf) + " nm is narrower than its contact allows (300 nm)");
    // Several fingers strap their sources below the channel, so a drain's strip must fit above that strap at its minimum area
    if (ng > 1 && wf < 440) throw std::invalid_argument("several fingers need at least 440 nm each, for their source strap");

    Generated g;
    auto add = [&](const LayerKey& k, const Rect& r) { g.shapes.push_back(rectShape(k, r)); };
    const int64_t pitch = l + 2 * kContToGate + kContSize;     // one gate and one contact column
    const int64_t firstCont = kActivOverCont, firstGate = firstCont + kContSize + kContToGate;
    const int64_t activX1 = firstCont + ng * pitch + kContSize + kActivOverCont;
    add(kActiv, {0, 0, activX1, wf});

    // Source and drain columns: contacts up the finger, a Metal1 strip over them, grown downward to its minimum area
    const int rows = (int)std::max<int64_t>(1, (wf - 2 * kActivOverCont + kContSpace) / (kContSize + kContSpace));
    const int64_t stack = rows * kContSize + (rows - 1) * kContSpace;
    const int64_t y0 = floorToGrid((wf - stack) / 2, t.grid);
    const int64_t stripWidth = kContSize + 2 * kMetalOverCont;
    const int64_t stripTop = y0 + stack + kMetalOverCont;
    int64_t stripBottom = y0 - kMetalOverCont;
    if ((stripTop - stripBottom) * stripWidth < kMetalArea) stripBottom = stripTop - ceilToGrid(kMetalArea / stripWidth + 1, t.grid);

    const int64_t strapTop = -kGateEndcap;  // the source strap, under the gates' ends
    const int64_t strapBottom = strapTop - kContSize;
    std::vector<int64_t> columns;
    for (int c = 0; c <= ng; ++c) columns.push_back(firstCont + c * pitch);
    for (size_t c = 0; c < columns.size(); ++c) {
        const int64_t x = columns[c];
        for (int r = 0; r < rows; ++r) {
            const int64_t cy = y0 + r * (kContSize + kContSpace);
            add(kCont, {x, cy, x + kContSize, cy + kContSize});
        }
        const bool source = c % 2 == 0;
        const Rect strip{x - kMetalOverCont, ng > 1 && source ? strapBottom : stripBottom, x + kContSize + kMetalOverCont, stripTop};
        add(kMetal1, strip);
        if (ng == 1) g.terminals[source ? "S" : "D"].push_back({0, strip});
    }
    if (ng > 1) {
        const int64_t lastSource = columns[(ng / 2) * 2];
        const Rect strap{columns[0] - kMetalOverCont, strapBottom, lastSource + kContSize + kMetalOverCont, strapTop};
        add(kMetal1, strap);
        g.terminals["S"].push_back({0, strap});
        // The drains join on Metal2, over vias at the top of their strips
        const int lastDrain = (ng - 1) / 2 * 2 + 1;
        const TechVia& via = t.vias[0];
        const int64_t viaBottom = stripTop - via.endcapLower - via.cut;
        const int64_t viaX0 = columns[1] + (kContSize - via.cut) / 2;
        for (int c = 1; c <= lastDrain; c += 2) {
            const int64_t vx = columns[c] + (kContSize - via.cut) / 2;
            add({via.cutLayer, 0}, {floorToGrid(vx, t.grid), viaBottom, floorToGrid(vx, t.grid) + via.cut, viaBottom + via.cut});
        }
        Rect m2{floorToGrid(viaX0, t.grid) - via.endcapUpper, viaBottom - via.endcapUpper,
                floorToGrid(columns[lastDrain] + (kContSize - via.cut) / 2, t.grid) + via.cut + via.endcapUpper, viaBottom + via.cut + via.endcapUpper};
        const int64_t m2Width = ceilToGrid(t.metals[1].minArea / (m2.y1 - m2.y0) + 1, t.grid);  // M2.d
        if (m2.x1 - m2.x0 < m2Width) {
            const int64_t grow = ceilToGrid((m2Width - (m2.x1 - m2.x0) + 1) / 2, t.grid);
            m2.x0 -= grow;
            m2.x1 += grow;
        }
        add({t.metals[1].layer, 0}, m2);
        g.terminals["D"].push_back({1, m2});
    }

    // Gates, with their contacts above the drains' strips by the metal's spacing and off the channel by Cnt.e, strapped
    // in GatPoly
    const int64_t contY = std::max(stripTop + kMetalSpace + kMetalOverCont, wf + kContOnPolyToActiv);
    const int64_t padY0 = contY - kPolyOverCont, padY1 = contY + kContSize + kPolyOverCont;
    const int64_t padWidth = kContSize + 2 * kPolyOverCont;
    int64_t padLeft = 0, padRight = 0, contLeft = 0, contRight = 0;
    for (int f = 0; f < ng; ++f) {
        const int64_t gx = firstGate + f * pitch;
        add(kGatPoly, {gx, -kGateEndcap, gx + l, padY0});
        const int64_t cx = floorToGrid(gx + (l - kContSize) / 2, t.grid);
        add(kCont, {cx, contY, cx + kContSize, contY + kContSize});
        const int64_t px0 = std::min(gx, cx - kPolyOverCont), px1 = std::max(gx + l, cx - kPolyOverCont + padWidth);
        if (f == 0) { padLeft = px0; contLeft = cx; }
        padRight = px1;
        contRight = cx;
    }
    add(kGatPoly, {padLeft, padY0, padRight, padY1});
    // Wide enough for the minimum area over one contact
    const int64_t metalHeight = kContSize + 2 * kMetalOverCont;
    int64_t mx0 = contLeft - kMetalOverCont, mx1 = contRight + kContSize + kMetalOverCont;
    if ((mx1 - mx0) * metalHeight < kMetalArea) {
        const int64_t more = ceilToGrid((kMetalArea / metalHeight + 1 - (mx1 - mx0) + 1) / 2, t.grid);
        mx0 -= more;
        mx1 += more;
    }
    const Rect gate{mx0, contY - kMetalOverCont, mx1, contY + kContSize + kMetalOverCont};
    add(kMetal1, gate);
    g.terminals["G"].push_back({0, gate});

    if (pmos) {
        add(kPsd, {-kPsdOverActiv, -kPsdOverGate, activX1 + kPsdOverActiv, wf + kPsdOverGate});
        add(kNwell, {-kNwellOverActiv, -kNwellOverActiv, activX1 + kNwellOverActiv, wf + kNwellOverActiv});
    }
    return g;
}

Generated resCell(const Tech& t, const std::string& kind, int64_t w, int64_t l) {
    if (t.name != "sg13g2") throw std::invalid_argument("no resistor generator for process " + t.name);
    const bool sil = kind == "rsil", high = kind == "rhigh";
    if (!sil && !high && kind != "rppd") throw std::invalid_argument("a poly resistor is rsil, rppd or rhigh");
    if (w < kResWidth) throw std::invalid_argument("width " + std::to_string(w) + " nm is below the process's 500 nm");
    if (l < kResLength) throw std::invalid_argument("length " + std::to_string(l) + " nm is below the process's 500 nm");
    if (w % t.grid || l % t.grid) throw std::invalid_argument("width and length must be on the " + std::to_string(t.grid) + " nm grid");

    Generated g;
    auto add = [&](const LayerKey& k, const Rect& r) { g.shapes.push_back(rectShape(k, r)); };
    // The body from y = 0 to l; a head at each end, long enough for its contacts past the body's blocking layer
    const int64_t gap = sil ? kResToCont : kSalToCont;
    const int64_t head = gap + kContSize + kPolyOverCont;
    add(kGatPoly, {0, -head, w, l + head});
    add(kPolyRes, {0, 0, w, l});
    if (sil) add(kRes, {0, 0, w, l});
    else add(kSalBlock, {-kSalOverPoly, 0, w + kSalOverPoly, l});
    const Rect around{-kImplantOverPoly, -head - kImplantOverPoly, w + kImplantOverPoly, l + head + kImplantOverPoly};
    add(kExtBlock, around);
    if (!sil) add(kPsd, around);
    if (high) add(kNsd, around);

    const int cols = (int)std::max<int64_t>(1, (w - 2 * kPolyOverCont + kContSpace) / (kContSize + kContSpace));
    const int64_t row = cols * kContSize + (cols - 1) * kContSpace;
    const int64_t x0 = floorToGrid((w - row) / 2, t.grid);
    for (const bool top : {false, true}) {
        const int64_t cy = top ? l + gap : -gap - kContSize;
        for (int c = 0; c < cols; ++c) add(kCont, {x0 + c * (kContSize + kContSpace), cy, x0 + c * (kContSize + kContSpace) + kContSize, cy + kContSize});
        Rect metal{x0 - kMetalOverCont, cy - kMetalOverCont, x0 + row + kMetalOverCont, cy + kContSize + kMetalOverCont};
        // Its minimum area, grown away from the body
        const int64_t height = ceilToGrid(kMetalArea / (metal.x1 - metal.x0) + 1, t.grid);
        if (metal.y1 - metal.y0 < height) (top ? metal.y1 : metal.y0) = top ? metal.y0 + height : metal.y1 - height;
        add(kMetal1, metal);
        g.terminals[top ? "PLUS" : "MINUS"].push_back({0, metal});
    }
    return g;
}

Generated capCell(const Tech& t, int64_t w, int64_t l) {
    if (t.name != "sg13g2") throw std::invalid_argument("no capacitor generator for process " + t.name);
    const LayerKey kMim{36, 0}, kVmim{129, 0}, kMetal5{67, 0}, kTopMetal1{126, 0};
    constexpr int64_t kTopWidth = 1640;     // TM1.a: the top plate is the MIM itself
    constexpr int64_t kVia = 420, kViaSpace = 420, kTopOverVia = 420;  // TV1.a, TV1.b, TV1.d
    constexpr int64_t kBottomOverMim = 600;  // the LVS deck's bottom plate: Metal5 within 0.6 um of the MIM
    constexpr double kMaxArea = 5625e6;      // MIM.g, nm2
    if (w < kTopWidth || l < kTopWidth) throw std::invalid_argument("a MIM capacitor is at least 1640 nm a side here (its TopMetal1 plate's minimum width)");
    if ((double)w * (double)l > kMaxArea) throw std::invalid_argument("a MIM capacitor is at most 5625 um2 (MIM.g)");
    if (w % t.grid || l % t.grid) throw std::invalid_argument("width and length must be on the " + std::to_string(t.grid) + " nm grid");
    Generated g;
    auto add = [&](const LayerKey& k, const Rect& r) { g.shapes.push_back(rectShape(k, r)); };
    const Rect mim{0, 0, w, l};
    add(kMim, mim);
    const Rect top = mim, bottom{-kBottomOverMim, -kBottomOverMim, w + kBottomOverMim, l + kBottomOverMim};
    add(kTopMetal1, top);
    add(kMetal5, bottom);
    // As many vias as fit kTopOverVia inside the plate, centred
    const auto count = [&](int64_t span) { return (int)std::max<int64_t>(1, (span - 2 * kTopOverVia + kViaSpace) / (kVia + kViaSpace)); };
    const int cols = count(w), rows = count(l);
    const int64_t x0 = floorToGrid((w - (cols * kVia + (cols - 1) * kViaSpace)) / 2, t.grid), y0 = floorToGrid((l - (rows * kVia + (rows - 1) * kViaSpace)) / 2, t.grid);
    for (int c = 0; c < cols; ++c)
        for (int r = 0; r < rows; ++r) {
            const int64_t x = x0 + c * (kVia + kViaSpace), y = y0 + r * (kVia + kViaSpace);
            add(kVmim, {x, y, x + kVia, y + kVia});
        }
    g.terminals["PLUS"].push_back({metalIndex(t, "TopMetal1"), top});
    g.terminals["MINUS"].push_back({metalIndex(t, "Metal5"), bottom});
    return g;
}

Generated tapCell(const Tech& t, bool ntap) {
    if (t.name != "sg13g2") throw std::invalid_argument("no tie generator for process " + t.name);
    Generated g;
    auto add = [&](const LayerKey& k, const Rect& r) { g.shapes.push_back(rectShape(k, r)); };
    const int64_t activW = kContSize + 2 * kActivOverCont, activH = 410;  // Act.d: at least 0.122 um2
    const int64_t cy = floorToGrid((activH - kContSize) / 2, t.grid);
    add(kActiv, {0, 0, activW, activH});
    add(kCont, {kActivOverCont, cy, kActivOverCont + kContSize, cy + kContSize});
    const Rect metal{kActivOverCont - kMetalOverCont, cy - 95, kActivOverCont + kContSize + kMetalOverCont, cy + kContSize + 95};  // M1.d
    add(kMetal1, metal);
    if (ntap) add(kNwell, {-kNwellOverActiv, -kNwellOverActiv, activW + kNwellOverActiv, activH + kNwellOverActiv});
    else add(kPsd, {-kPsdOverActiv, -kPsdOverActiv, activW + kPsdOverActiv, activH + kPsdOverActiv});
    g.terminals["B"].push_back({0, metal});
    return g;
}

CellExtent extentOf(const Generated& g) {
    CellExtent e{INT64_MAX, INT64_MAX, INT64_MIN, INT64_MIN};
    for (const auto& s : g.shapes)
        for (const auto& p : s.points) {
            e.x0 = std::min(e.x0, p.x);
            e.y0 = std::min(e.y0, p.y);
            e.x1 = std::max(e.x1, p.x);
            e.y1 = std::max(e.y1, p.y);
        }
    return e;
}

}  // namespace layout
