// ============================================================================
// Phase 14.2: Bi-Directional Parsing (Manual Sync)
// ============================================================================
#include "schematic_editor.h"
#include <QRegularExpression>
#include <QJsonObject>

/**
 * Generates a Verilog-AMS representation of the current schematic.
 * Iterates through the netlist and formats components as module instantiations.
 */
QString SchematicEditor::generateVerilogSnippet() {
    TensorNetlist netlist = scene->generateNetlist();
    QString code;
    
    // Header
    code += "// Auto-Generated from Schematic\n";
    code += "module top_schematic();\n\n";
    
    // Wires (Nodes) - Naive enumeration
    // In a real system, we'd use named nets from labels
    code += "  // Nets\n";
    int maxNode = 0;
    // ... logic to find max node ...
    QStringList wires;
    // Iterate to find unique nets. For now, assume a few generic ones.
    
    // Components
    code += "  // Component Instantiations\n";
    
    // Resistors
    for (size_t i = 0; i < netlist.globalBlock.resistors.size(); ++i) {
        const auto& comp = netlist.globalBlock.resistors[i];
        QString name = "R" + QString::number(i); 
        code += QString("  resistor #(.r(%1)) %2 (.p(n%3), .n(n%4));\n")
                .arg(comp.resistance_ohms)
                .arg(name)
                .arg(comp.nodeTerminal1)
                .arg(comp.nodeTerminal2);
    }
    
    // Capacitors
    for (size_t i = 0; i < netlist.globalBlock.capacitors.size(); ++i) {
        const auto& comp = netlist.globalBlock.capacitors[i];
        QString name = "C" + QString::number(i);
        code += QString("  capacitor #(.c(%1)) %2 (.p(n%3), .n(n%4));\n")
                .arg(comp.capacitance_farads)
                .arg(name)
                .arg(comp.nodePlate1)
                .arg(comp.nodePlate2);
    }
    
    // Voltage Sources
    for (size_t i = 0; i < netlist.globalBlock.voltageSources.size(); ++i) {
        const auto& comp = netlist.globalBlock.voltageSources[i];
        QString name = "V" + QString::number(i);
        code += QString("  vsource #(.v(%1)) %2 (.p(n%3), .n(n%4));\n")
                .arg(comp.voltage_V)
                .arg(name)
                .arg(comp.nodePositive)
                .arg(comp.nodeNegative);
    }
    
    // MOSFETs
    for (size_t i = 0; i < netlist.globalBlock.mosfets.size(); ++i) {
        const auto& comp = netlist.globalBlock.mosfets[i];
        QString name = QString::fromStdString(comp.instanceName);
        if (name.isEmpty()) name = "M" + QString::number(i);
        
        QString type = "nmos"; // Default
        if (comp.modelName.find("PMOS") != std::string::npos) type = "pmos";
        
        code += QString("  %1 #(.w(%2), .l(%3)) %4 (.d(n%5), .g(n%6), .s(n%7), .b(n%8));\n")
                .arg(type)
                .arg(comp.channelWidth_m)
                .arg(comp.channelLength_m)
                .arg(name)
                .arg(comp.drain)
                .arg(comp.gate)
                .arg(comp.source)
                .arg(comp.body);
    }
    
    // Current Sources
    for (size_t i = 0; i < netlist.globalBlock.currentSources.size(); ++i) {
        const auto& comp = netlist.globalBlock.currentSources[i];
        QString name = "I" + QString::number(i);
        code += QString("  isource #(.i(%1)) %2 (.p(n%3), .n(n%4));\n")
                .arg(comp.current_A)
                .arg(name)
                .arg(comp.nodePositive)
                .arg(comp.nodeNegative);
    }
    
    // Diodes
    for (size_t i = 0; i < netlist.globalBlock.diodes.size(); ++i) {
        const auto& comp = netlist.globalBlock.diodes[i];
        QString name = "D" + QString::number(i);
        code += QString("  diode #(.is(%1), .n(%2)) %3 (.a(n%4), .c(n%5));\n")
                .arg(comp.saturationCurrent_I_S_A)
                .arg(comp.emissionCoefficient_N)
                .arg(name)
                .arg(comp.anode)
                .arg(comp.cathode);
    }
    
    // BJTs
    for (size_t i = 0; i < netlist.globalBlock.bjts.size(); ++i) {
        const auto& comp = netlist.globalBlock.bjts[i];
        QString name = QString::fromStdString(comp.instanceName);
        if (name.isEmpty()) name = "Q" + QString::number(i);
        
        QString type = comp.isNPN ? "npn" : "pnp";
        code += QString("  %1 #(.bf(%2), .is(%3)) %4 (.c(n%5), .b(n%6), .e(n%7));\n")
                .arg(type)
                .arg(comp.betaF)
                .arg(comp.saturationCurrent_I_S_A)
                .arg(name)
                .arg(comp.nodeCollector)
                .arg(comp.base)
                .arg(comp.emitter);
    }
    
    // Inductors
    for (size_t i = 0; i < netlist.globalBlock.inductors.size(); ++i) {
        const auto& comp = netlist.globalBlock.inductors[i];
        QString name = "L" + QString::number(i);
        code += QString("  inductor #(.l(%1)) %2 (.p(n%3), .n(n%4));\n")
                .arg(comp.inductance_henries)
                .arg(name)
                .arg(comp.nodeCoil1)
                .arg(comp.nodeCoil2);
    }
    
    // Hierarchical Block Instances (including PDK Standard Cells)
    for (size_t i = 0; i < netlist.instances.size(); ++i) {
        const auto& inst = netlist.instances[i];
        QString name = "U" + QString::number(i);
        QString type = QString::fromStdString(inst.blockName);
        
        code += QString("  %1 %2 (").arg(type).arg(name);
        
        // Map pins to global nodes
        // In our StandardCellLibrary, pin nodes are typically 1..pinCount
        // These are the first N elements in nodeMapping.
        
        const auto& nodes = inst.nodeMapping;
        for (size_t j = 0; j < nodes.size(); ++j) {
            code += QString(".p%1(n%2)").arg(j+1).arg(nodes[j]);
            if (j < nodes.size() - 1) code += ", ";
        }
        code += ");\n";
    }

    code += "\nendmodule\n";
    return code;
}

/**
 * Parses Verilog-AMS code and instantiates components in the schematic.
 * Uses Regex to identify module instantiations and placement "Ghost Boxes".
 */
void SchematicEditor::parseVerilogToSchematic(const QString& code) {
    // 1. Resistor Regex: resistor #(.r(1000)) R1 (.p(n1), .n(n2));
    QRegularExpression resRegex(R"(resistor\s*#\s*\(\.r\(([\d\.e\-\+]+)\)\)\s*(\w+)\s*\(\.p\((\w+)\),\s*\.n\((\w+)\)\);)", QRegularExpression::CaseInsensitiveOption);
    
    QRegularExpressionMatchIterator i = resRegex.globalMatch(code);
    int xArg = 100;
    
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString valStr = match.captured(1);
        QString name = match.captured(2);
        
        // Check if component already exists (Synch not dupe)
        if (findComponent(name)) continue;
        
        // Place new component
        // Ideally we use Graphviz/Autolayout here. For now, linear placement.
        QJsonObject params;
        params["resistance"] = valStr.toDouble();
        
        placeComponent("resistor", xArg, 100, name, "");
        VisualComponent* comp = findComponent(name);
        if (comp) {
            comp->setOpacity(0.5); // Ghost look until moved
        }
        
        xArg += 100;
    }
    
    // TODO: Add regex for other components (Mosfet, Cap)
    scene->update();
}
