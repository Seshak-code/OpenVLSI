#pragma once
// The netlister (docs/rebuild.md, M1): a schematic's hierarchy as SPICE for TreeSPICE.
//
// Each instance's master cell is switched to a view by the view list (Cadence's config): "spice" means the cell is a
// primitive, netlisted by its `spice` cell property; another name is a schematic view to descend into, netlisted as a
// .subckt. The default list is {"spice", "schematic"}. A config view (type config) sets it with properties:
//   top       "lib/cell/view"              the schematic to netlist
//   viewList  ["schematic", "spice", ...]  the order views are tried in
//   bindings  {"lib/cell": "view", ...}    a cell's view, ahead of the list
//
// A primitive's `spice` cell property: {"prefix": "M", "template": "[|D] [|G] [|S] [|B] [@model] [@w:w=%]"} where
// [|pin] is the net on that pin, [@p] the instance's parameter p (else the cell's `params` default; neither is an
// error), [@p:text] the text with % replaced by p's value (nothing when p has none), [@instanceName], [@cellName].
// The element name is the prefix and the instance name (the name alone when it already starts with the prefix).
// Optional: "models": [{file, section}] the model files it needs (a .osdi file, a .lib section, else .include), and
// "globals": {name: value} the .params its template uses (mc_mismatch = 0, say).
//
// Nets keep their schematic names; a global (name!) is .global, gnd! is node 0. Fail closed: a connectivity error, a
// symbol whose pins differ from its schematic's, a cell with no view in the list, a hierarchy loop, two nets or two
// elements that SPICE would merge (it ignores case), a local net SPICE would read as ground, are errors.
#include "oadb/oadb.h"

#include <map>
#include <string>
#include <vector>

namespace sim {

struct Model {
    std::string file, section;  // section "": .include (or .osdi for a .osdi file)
};
std::string modelCard(const Model& m);  // .osdi "file" / .lib "file" section / .include "file"

struct Deck {
    std::string text;                              // the title, .global, the .subckts and the elements
    std::vector<Model> models;                     // the primitives' model files, in first-use order
    std::map<std::string, std::string> globals;    // the primitives' .param defaults
};
// lib/cell/view: a schematic, or a config view naming one, in parts (ADE replaces model sections and overrides
// globals with its variables)
Deck deck(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view);

// The deck whole: its text, the globals as .params, its models. It ends before .end (a test bench adds its cards).
std::string netlist(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view);

}  // namespace sim
