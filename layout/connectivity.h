#pragma once
// A layout view against the schematic it was generated from: which of its nets are wired, which are still flight lines,
// and which are shorted. An editor's running check; sign-off is LVS's.
#include "layout/pcell.h"

#include <string>
#include <vector>

namespace layout {

struct NetStatus {
    std::string name;
    int terminals = 0;
    int groups = 0;  // separately wired groups of its terminals: 1 is connected
    std::vector<Rect> flightLines;  // from group to group: x0, y0 to x1, y1, in database units
};

struct ConnectivityReport {
    std::vector<NetStatus> nets;  // sorted by name
    std::vector<std::pair<std::string, std::string>> shorts;
    bool clean() const;
};

// `connectivity` is the view's prop written by layout_generate, as JSON text. Terminals are the rectangles the view's
// instances and pins declare; the wires are the view's own metal and via shapes, by bounding box.
// ponytail: wires join by bounding box, so a diagonal or L polygon can read as touching what it only passes near; LVS decides.
ConnectivityReport checkConnectivity(oadb::Library& lib, const Tech& tech, const std::string& cell, const std::string& view);

}  // namespace layout
