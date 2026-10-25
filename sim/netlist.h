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
//
// Nets keep their schematic names; a global (name!) is .global, gnd! is node 0. Fail closed: a connectivity error, a
// symbol whose pins differ from its schematic's, a cell with no view in the list, a hierarchy loop, two nets or two
// elements that SPICE would merge (it ignores case), a local net SPICE would read as ground, are errors.
#include "oadb/oadb.h"

#include <string>

namespace sim {

// lib/cell/view: a schematic, or a config view naming one. The text ends before .end (a test bench adds its cards).
std::string netlist(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view);

}  // namespace sim
