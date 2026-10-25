#pragma once
// Schematic semantics over oadb (docs/rebuild.md, M1): instance placement, connectivity, symbol generation.
//
// Connectivity follows the schematic editor's rules (Virtuoso's): a wire joins another where one's END lies on the
// other (endpoint or T-junction); wires that merely cross do not connect. A pin (an instance's, through its master
// symbol and its placement, or the schematic's own) joins a wire whose end is on it, or another pin at the same
// point. A net label names the wire it sits on (or the pin it sits on); labels and pins with the same name are one
// net; a name ending in '!' is global. Two names on one net (a short) and a label on nothing are errors; a floating
// pin, a dangling wire end and a schematic whose pins differ from its symbol's are warnings.
#include "oadb/oadb.h"

#include <map>
#include <string>
#include <vector>

namespace sch {

// p under an orientation about the origin (y up; R90 turns counter-clockwise; MY mirrors x, MX mirrors y, MYR90 and
// MXR90 mirror first, then turn by 90)
oadb::Point transform(const std::string& orient, oadb::Point p);
inline oadb::Point place(const oadb::Instance& i, oadb::Point p) {
    const oadb::Point t = transform(i.orient, p);
    return {t.x + i.at.x, t.y + i.at.y};
}

struct Terminal {
    std::string instance;  // "": the schematic's own pin
    std::string pin;
    bool operator==(const Terminal& o) const { return instance == o.instance && pin == o.pin; }
};

struct Net {
    std::string name;
    bool global = false;  // the name ends in '!'
    std::vector<Terminal> terminals;  // sorted
};

struct Connectivity {
    std::vector<Net> nets;  // sorted by name
    std::map<std::string, std::map<std::string, std::string>> instanceNets;  // instance -> its pin -> net
    std::map<int64_t, std::string> wireNets, labelNets;                      // a wire's or a label's id -> its net
    std::vector<std::string> errors, warnings;
    bool ok() const { return errors.empty(); }
};

// The connectivity of lib/cell/view (a schematic); masters resolve through the workspace's libraries
Connectivity extract(oadb::Workspace& ws, const std::string& lib, const std::string& cell, const std::string& view);

// A symbol view `to` from the schematic view `from`'s pins: inputs left, outputs right, inputOutput on top, a box,
// stubs, pin names and [@instanceName] / [@cellName] labels. One undo step; an existing `to` is an error.
void generateSymbol(oadb::Library& lib, const std::string& cell, const std::string& from, const std::string& to);

}  // namespace sch
