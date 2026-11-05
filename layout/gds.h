#pragma once
// GDSII stream, read and written. A library is its structures as oadb layout objects, so an import is one loop and an
// export the same loop backwards. Fail closed: a truncated file, an unknown record or an absolute-magnification
// reference is an error naming its offset. NODE elements have no geometry a mask needs: they are counted and dropped.
#include "oadb/oadb.h"

#include <string>
#include <vector>

namespace layout {

struct GdsCell {
    std::string name;
    std::vector<oadb::LayoutShape> shapes;
    std::vector<oadb::LayoutInstance> instances;  // lib and view empty: a structure of this library
};

struct GdsLibrary {
    std::string name = "lib";
    double userPerDbu = 1e-3;     // UNITS' first number: a database unit in user units
    double metersPerDbu = 1e-9;   // and its second
    std::vector<GdsCell> cells;
    int nodesDropped = 0;
};

GdsLibrary readGds(const std::string& path);
void writeGds(const std::string& path, const GdsLibrary& lib);

}  // namespace layout
