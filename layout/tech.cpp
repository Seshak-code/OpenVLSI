#include "layout/tech.h"

#include <cstdlib>

namespace layout {

namespace {

// IHP SG13G2. Numbers from the PDK's KLayout layer properties, rules from its sg13g2_maximal.lydrc (rule names beside them)
Tech sg13g2() {
    Tech t;
    t.name = "sg13g2";
    t.metersPerDbu = 1e-9;
    t.grid = 5;
    t.pinDatatype = 2;
    t.textDatatype = 25;
    t.layers = {
        {"NWell", 31, 0, "#268c6b"},    {"Activ", 1, 0, "#00ff00"},       {"pSD", 14, 0, "#ccb899"},        {"GatPoly", 5, 0, "#bf4026"},
        {"Cont", 6, 0, "#00ffff"},      {"Metal1", 8, 0, "#39bfff"},      {"Metal1.pin", 8, 2, "#39bfff"},  {"Metal1.text", 8, 25, "#39bfff"},
        {"Via1", 19, 0, "#ccccff"},     {"Metal2", 10, 0, "#ccccd9"},     {"Metal2.pin", 10, 2, "#ccccd9"}, {"Metal2.text", 10, 25, "#ccccd9"},
        {"Via2", 29, 0, "#ff3736"},     {"Metal3", 30, 0, "#d80000"},     {"Metal3.pin", 30, 2, "#d80000"}, {"Metal3.text", 30, 25, "#d80000"},
        {"Via3", 49, 0, "#9ba940"},     {"Metal4", 50, 0, "#93e837"},     {"Metal4.pin", 50, 2, "#93e837"}, {"Metal4.text", 50, 25, "#93e837"},
        {"Via4", 66, 0, "#deac5e"},     {"Metal5", 67, 0, "#dcd146"},     {"Metal5.pin", 67, 2, "#dcd146"}, {"Metal5.text", 67, 25, "#dcd146"},
    };
    t.metals = {
        {"Metal1", 8, 160, 180, 90000},    // M1.a, M1.b, M1.d
        {"Metal2", 10, 200, 210, 144000},  // M2.a, M2.b, M2.d
        {"Metal3", 30, 200, 210, 144000},
        {"Metal4", 50, 200, 210, 144000},
        {"Metal5", 67, 200, 210, 144000},
    };
    // V1.a, V1.b, V1.c, V1.c1; M2.c, M2.c1. Via2 to Via4 keep the upper metal's numbers on their lower metal too (V2.c).
    t.vias = {
        {"Via1", 19, 0, 1, 190, 220, 10, 50, 5, 50},
        {"Via2", 29, 1, 2, 190, 220, 5, 50, 5, 50},
        {"Via3", 49, 2, 3, 190, 220, 5, 50, 5, 50},
        {"Via4", 66, 3, 4, 190, 220, 5, 50, 5, 50},
    };
    return t;
}

const std::vector<Tech>& all() {
    static const std::vector<Tech> techs = {sg13g2()};
    return techs;
}

}  // namespace

const Tech* findTech(const std::string& name) {
    for (const auto& t : all())
        if (t.name == name) return &t;
    return nullptr;
}

std::vector<std::string> techNames() {
    std::vector<std::string> out;
    for (const auto& t : all()) out.push_back(t.name);
    return out;
}

std::optional<LayerKey> layerNamed(const Tech* tech, const std::string& name) {
    const size_t slash = name.find('/');
    if (slash != std::string::npos) {
        char* end = nullptr;
        const long layer = std::strtol(name.c_str(), &end, 10);
        if (end != name.c_str() + slash) return std::nullopt;
        const long datatype = std::strtol(name.c_str() + slash + 1, &end, 10);
        if (*end || end == name.c_str() + slash + 1 || layer < 0 || layer > 32767 || datatype < 0 || datatype > 32767) return std::nullopt;
        return LayerKey{(int)layer, (int)datatype};
    }
    if (tech)
        for (const auto& l : tech->layers)
            if (l.name == name) return LayerKey{l.layer, l.datatype};
    return std::nullopt;
}

int metalIndex(const Tech& tech, const std::string& name) {
    for (size_t k = 0; k < tech.metals.size(); ++k)
        if (tech.metals[k].name == name) return (int)k;
    return -1;
}

}  // namespace layout
