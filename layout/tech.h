#pragma once
// What a layout editor must know of a process: its layers, metals and vias with their design rules, and how to draw a
// transistor. One table per process, read off its own DRC deck; a library attaches one by name (layout_tech_set).
#include "layout/flatten.h"

#include <optional>
#include <string>
#include <vector>

namespace layout {

struct TechLayer { std::string name; int layer, datatype; std::string color; };

struct TechMetal {
    std::string name;
    int layer;
    int64_t minWidth, minSpace, minArea;  // database units, and their square
};

struct TechVia {
    std::string name;
    int cutLayer;
    int lower, upper;                     // indices into Tech::metals
    int64_t cut, space;                   // the cut's size and spacing
    int64_t enclosureLower, endcapLower;  // the lower metal's minimum enclosure, and on the two sides that are its endcap
    int64_t enclosureUpper, endcapUpper;
};

struct Tech {
    std::string name;
    double metersPerDbu;
    int64_t grid;                         // the manufacturing grid
    int pinDatatype, textDatatype;        // a metal's pin rectangles and labels
    std::vector<TechLayer> layers;        // the palette, in drawing order
    std::vector<TechMetal> metals;        // bottom up
    std::vector<TechVia> vias;            // vias[k] joins metals[k] and metals[k + 1]
};

const Tech* findTech(const std::string& name);  // nullptr when there is no such process
std::vector<std::string> techNames();

// "Metal1" (its drawing layer), "Metal1.pin", or numbers "8/0". Nothing when the name is not one.
std::optional<LayerKey> layerNamed(const Tech* tech, const std::string& name);
int metalIndex(const Tech& tech, const std::string& name);  // -1 when it is no metal

}  // namespace layout
