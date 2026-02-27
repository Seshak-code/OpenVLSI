// ============================================================================
// Phase 14.2: Bi-Directional Parsing (Manual Sync)
// ============================================================================
#include "schematic_editor.h"
#include <QRegularExpression>
#include <QJsonObject>
#include "../../../components/netlist_export.h"

/**
 * Generates a Verilog-AMS representation of the current schematic.
 * Iterates through the netlist and formats components as module instantiations.
 */
QString SchematicEditor::generateVerilogSnippet() {
    auto hierDto = toSchematicGraph();
    
    NetlistExporter::ExportOptions options;
    options.topModuleName = "top_schematic";
    options.flavor = NetlistExporter::ExportFlavor::AnalogMixedSignal;
    
    std::string code = NetlistExporter::toVerilog(hierDto, options);
    return QString::fromStdString(code);
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
