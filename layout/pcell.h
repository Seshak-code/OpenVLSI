#pragma once
// Generated geometry: a via, a substrate or well tie, and a transistor, each drawn from a process's rules. Checked in
// tests/test_layout_edit against the PDK's own DRC and LVS decks.
#include "layout/tech.h"

#include <map>
#include <string>
#include <vector>

namespace layout {

struct Rect { int64_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

oadb::LayoutShape rectShape(const LayerKey& layer, const Rect& r);

struct Generated {
    std::vector<oadb::LayoutShape> shapes;
    std::map<std::string, std::vector<std::pair<int, Rect>>> terminals;  // name -> (metal index, rectangle) it can be reached on
};

// A cut array at a point, centred, with its metals' enclosures: the long side of each metal carries its endcap
std::vector<oadb::LayoutShape> viaShapes(const Tech& t, int via, int64_t x, int64_t y, int cols = 1, int rows = 1);

// SG13G2 low-voltage MOS. w is the total width over ng fingers, l the gate length, in database units. Throws with the
// reason when the size cannot be drawn: fingers narrower than contacts allow, or off the manufacturing grid.
// ponytail: no dogbone, so fingers narrower than 300 nm are refused though the process allows 150 nm; the upgrade is a
// contact landing outside the channel.
Generated mosCell(const Tech& t, bool pmos, int64_t w, int64_t l, int ng);
// The tie of a body: a contact in Activ, in pSD for the substrate (ntap false), or in NWell (ntap true)
Generated tapCell(const Tech& t, bool ntap);

// What the two rows of layout_generate must keep clear of, as the cell's own extents (all layers)
struct CellExtent { int64_t x0, y0, x1, y1; };
CellExtent extentOf(const Generated& g);

}  // namespace layout
