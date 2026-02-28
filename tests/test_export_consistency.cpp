#include <iostream>
#include <cassert>
#include <sstream>
#include <set>
#include "acutesim_engine/netlist/circuit.h"
#include "acutesim_engine/netlist/netlist_export.h"

// Helper to extract port names from Verilog header
std::set<std::string> extractVerilogPorts(const std::string& vlog) {
    std::set<std::string> ports;
    // VERY simple parser for: module top (input a, output b, inout c);
    size_t start = vlog.find("(");
    size_t end = vlog.find(")");
    if (start == std::string::npos || end == std::string::npos) return ports;
    
    std::string content = vlog.substr(start+1, end-start-1);
    std::stringstream ss(content);
    std::string token;
    while(std::getline(ss, token, ',')) {
        // remove "input ", "output ", "inout " and trim
        // Token e.g. " input in"
        size_t lastSpace = token.find_last_of(" ");
        if (lastSpace != std::string::npos) {
            std::string name = token.substr(lastSpace+1);
            // Trim whitespace
            name.erase(0, name.find_first_not_of(" \n\r\t"));
            name.erase(name.find_last_not_of(" \n\r\t") + 1);
            ports.insert(name);
        }
    }
    return ports;
}

// Helper to extract port names from SPEF
std::set<std::string> extractSPEFPorts(const std::string& spef) {
    std::set<std::string> ports;
    size_t pStart = spef.find("*PORTS");
    size_t pEnd = spef.find("*END", pStart);
    if (pStart == std::string::npos || pEnd == std::string::npos) return ports;
    
    std::stringstream ss(spef.substr(pStart+6, pEnd-pStart-6));
    std::string line;
    while(std::getline(ss, line)) {
        std::stringstream ls(line);
        std::string name;
        ls >> name; // First token is name
        if(!name.empty()) ports.insert(name);
    }
    return ports;
}

void test_consistency() {
    std::cout << "[Test] Export Consistency (Verilog vs SPEF)... ";
    
    TensorNetlist netlist;
    netlist.globalBlock.name = "top";
    
    // Config
    NetlistExporter::ExportOptions opts;
    opts.topModuleName = "top";
    opts.powerNetNames["VDD"] = "VDD_CORE";
    opts.powerNetNames["VSS"] = "GND";
    
    // Generate
    std::string vlog = NetlistExporter::toVerilog(netlist, opts);
    std::string spef = NetlistExporter::toSPEF(netlist, opts);
    
    // Check Ports
    auto vPorts = extractVerilogPorts(vlog);
    auto sPorts = extractSPEFPorts(spef);
    
    // Verilog has: in, out, VDD_CORE, GND
    // SPEF has: in, out, VDD_CORE, GND
    // Check Intersection
    assert(vPorts.count("VDD_CORE"));
    assert(sPorts.count("VDD_CORE"));
    assert(vPorts.count("GND"));
    assert(sPorts.count("GND"));
    
    // Verify intersection size matches (all ports in SPEF must be in Verilog)
    for(const auto& p : sPorts) {
        assert(vPorts.count(p) && "Port in SPEF missing from Verilog!");
    }
    
    std::cout << "Passed." << std::endl;
}

int main() {
    test_consistency();
    return 0;
}
