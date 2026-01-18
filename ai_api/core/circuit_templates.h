#pragma once
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include "../../gui/core/gui_types.h"

/**
 * circuit_templates.h - Predefined Circuit Topology Templates
 * 
 * Contains JSON descriptions of common analog building blocks
 * that can be instantiated by the AI copilot.
 * 
 * Templates are MODE-AWARE:
 * - Bench: Uses Ground, VoltageSource, CurrentSource (ideal)
 * - Block: Uses PowerPort (interface pins, no sources)
 * - System: Uses PowerVdd, PowerVss, RailDriver (realistic)
 */

namespace AIAPI {

struct CircuitTemplate {
    QString name;
    QString description;
    QJsonArray components;  // List of { type, id, x, y, params }
    QJsonArray wires;       // List of connection paths
    QStringList ports;      // External connection points
};

class CircuitTemplates {
public:
    // Mode-aware template lookup (primary)
    static CircuitTemplate getTemplate(const QString& name, AppMode mode) {
        QString key = name.toLower() + "_" + modeKey(mode);
        static QMap<QString, CircuitTemplate> templates = initTemplates();
        if (templates.contains(key)) {
            return templates.value(key);
        }
        // Fallback to bench mode if mode-specific not found
        return templates.value(name.toLower() + "_bench", templates.value(name.toLower()));
    }
    
    // Legacy lookup (defaults to Bench)
    static CircuitTemplate getTemplate(const QString& name) {
        return getTemplate(name, AppMode::BENCH_TEST);
    }
    
    static QStringList availableTemplates() {
        return {"darlington", "current_mirror", "differential_pair", "inverter", "source_follower", "uln2003a"};
    }

private:
    static QString modeKey(AppMode mode) {
        switch (mode) {
            case AppMode::BENCH_TEST: return "bench";
            case AppMode::BLOCK_SUBSYSTEM: return "block";
            case AppMode::SYSTEM_INTEGRATION: return "system";
            default: return "bench";
        }
    }
    
    static QMap<QString, CircuitTemplate> initTemplates() {
        QMap<QString, CircuitTemplate> map;
        
        // ====== DARLINGTON PAIR (BENCH MODE) ======
        // Uses Ground, VoltageSource, CurrentSource (ideal)
        {
            CircuitTemplate t;
            t.name = "darlington";
            t.description = "Darlington Pair (NPN) - Bench Test with ideal sources";
            
            t.components = QJsonArray{
                QJsonObject{{"type", "VoltageSource"}, {"id", "V1"}, {"x", 0}, {"y", 100}, {"value", "10"}},
                QJsonObject{{"type", "CurrentSource"}, {"id", "I1"}, {"x", 100}, {"y", 200}, {"value", "1m"}},
                QJsonObject{{"type", "Ground"}, {"id", "GND"}, {"x", 200}, {"y", 340}},
                QJsonObject{{"type", "Resistor"}, {"id", "R1"}, {"x", 300}, {"y", 60}, {"value", "100"}, {"rotation", 90}},
                QJsonObject{{"type", "Bjt"}, {"id", "Q1"}, {"x", 260}, {"y", 140}, {"value", "NPN"}},
                QJsonObject{{"type", "Bjt"}, {"id", "Q2"}, {"x", 300}, {"y", 240}, {"value", "NPN"}}
            };
            
            t.wires = QJsonArray{
                QJsonObject{{"points", QJsonArray{QJsonArray{0, 60}, QJsonArray{0, 0}, QJsonArray{300, 0}, QJsonArray{300, 20}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{300, 100}, QJsonArray{300, 120}, QJsonArray{280, 120}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{300, 120}, QJsonArray{320, 120}, QJsonArray{320, 220}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{280, 160}, QJsonArray{280, 240}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{320, 260}, QJsonArray{320, 320}, QJsonArray{200, 320}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{200, 320}, QJsonArray{0, 320}, QJsonArray{0, 140}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{200, 320}, QJsonArray{100, 320}, QJsonArray{100, 240}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{100, 160}, QJsonArray{100, 140}, QJsonArray{240, 140}}}}
            };
            
            t.ports = {"V1.+", "GND", "Q1.B"};
            map["darlington_bench"] = t;
            map["darlington"] = t; // Default
        }
        
        // ====== DARLINGTON PAIR (SYSTEM MODE) ======
        // Uses PowerVdd, PowerVss, RailDriver (realistic power)
        {
            CircuitTemplate t;
            t.name = "darlington";
            t.description = "Darlington Pair (NPN) - System Integration with PDN";
            
            t.components = QJsonArray{
                QJsonObject{{"type", "RailDriver"}, {"id", "VCC"}, {"x", 0}, {"y", 100}, {"value", "10V"}},
                QJsonObject{{"type", "WaveformGen"}, {"id", "SIG"}, {"x", 100}, {"y", 200}, {"value", "1mA Pulse"}},
                QJsonObject{{"type", "PowerVss"}, {"id", "VSS"}, {"x", 200}, {"y", 340}, {"value", "0V"}},
                QJsonObject{{"type", "Resistor"}, {"id", "R1"}, {"x", 300}, {"y", 60}, {"value", "100"}, {"rotation", 90}},
                QJsonObject{{"type", "Bjt"}, {"id", "Q1"}, {"x", 260}, {"y", 140}, {"value", "NPN"}},
                QJsonObject{{"type", "Bjt"}, {"id", "Q2"}, {"x", 300}, {"y", 240}, {"value", "NPN"}}
            };
            
            t.wires = QJsonArray{
                QJsonObject{{"points", QJsonArray{QJsonArray{0, 60}, QJsonArray{0, 0}, QJsonArray{300, 0}, QJsonArray{300, 20}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{300, 100}, QJsonArray{300, 120}, QJsonArray{280, 120}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{300, 120}, QJsonArray{320, 120}, QJsonArray{320, 220}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{280, 160}, QJsonArray{280, 240}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{320, 260}, QJsonArray{320, 320}, QJsonArray{200, 320}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{200, 320}, QJsonArray{0, 320}, QJsonArray{0, 140}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{200, 320}, QJsonArray{100, 320}, QJsonArray{100, 240}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{100, 160}, QJsonArray{100, 140}, QJsonArray{240, 140}}}}
            };
            
            t.ports = {"VCC.+", "VSS", "Q1.B"};
            map["darlington_system"] = t;
        }
        
        // ====== DARLINGTON PAIR (BLOCK MODE) ======
        // Uses PowerPort (interface pins, no sources)
        {
            CircuitTemplate t;
            t.name = "darlington";
            t.description = "Darlington Pair (NPN) - Block Subsystem with PowerPorts";
            
            t.components = QJsonArray{
                QJsonObject{{"type", "PowerPort"}, {"id", "VCC"}, {"x", 0}, {"y", 0}, {"value", "VCC"}},
                QJsonObject{{"type", "PowerPort"}, {"id", "IN"}, {"x", 100}, {"y", 100}, {"value", "IN"}},
                QJsonObject{{"type", "PowerPort"}, {"id", "GND"}, {"x", 200}, {"y", 340}, {"value", "GND"}},
                QJsonObject{{"type", "Resistor"}, {"id", "R1"}, {"x", 300}, {"y", 60}, {"value", "100"}, {"rotation", 90}},
                QJsonObject{{"type", "Bjt"}, {"id", "Q1"}, {"x", 260}, {"y", 140}, {"value", "NPN"}},
                QJsonObject{{"type", "Bjt"}, {"id", "Q2"}, {"x", 300}, {"y", 240}, {"value", "NPN"}}
            };
            
            t.wires = QJsonArray{
                QJsonObject{{"points", QJsonArray{QJsonArray{0, 0}, QJsonArray{300, 0}, QJsonArray{300, 20}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{300, 100}, QJsonArray{300, 120}, QJsonArray{280, 120}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{300, 120}, QJsonArray{320, 120}, QJsonArray{320, 220}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{280, 160}, QJsonArray{280, 240}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{320, 260}, QJsonArray{320, 320}, QJsonArray{200, 320}}}},
                QJsonObject{{"points", QJsonArray{QJsonArray{100, 100}, QJsonArray{240, 140}}}}
            };
            
            t.ports = {"VCC", "GND", "IN"};
            map["darlington_block"] = t;
        }
        
        // ====== CURRENT MIRROR ======
        // Two matched transistors for current copying
        {
            CircuitTemplate t;
            t.name = "current_mirror";
            t.description = "Current Mirror (NMOS) - Copies reference current";
            t.components = QJsonArray{
                QJsonObject{{"type", "NMOS"}, {"id", "M1"}, {"x", 0}, {"y", 0}, {"params", QJsonObject{{"W", "10u"}, {"L", "1u"}}}},
                QJsonObject{{"type", "NMOS"}, {"id", "M2"}, {"x", 100}, {"y", 0}, {"params", QJsonObject{{"W", "10u"}, {"L", "1u"}}}}
            };
            t.wires = QJsonArray{
                // M1.Gate -> M2.Gate (shared bias)
                QJsonObject{{"from", "M1.G"}, {"to", "M2.G"}},
                // M1.Gate -> M1.Drain (diode connected)
                QJsonObject{{"from", "M1.G"}, {"to", "M1.D"}},
                // M1.Source -> M2.Source (common ground)
                QJsonObject{{"from", "M1.S"}, {"to", "M2.S"}}
            };
            t.ports = {"M1.D", "M2.D", "M1.S"}; // Iref, Iout, GND
            map["current_mirror"] = t;
        }
        
        // ====== DIFFERENTIAL PAIR ======
        // Two matched transistors for differential amplification
        {
            CircuitTemplate t;
            t.name = "differential_pair";
            t.description = "Differential Pair (NMOS) - Balanced input stage";
            t.components = QJsonArray{
                QJsonObject{{"type", "NMOS"}, {"id", "M1"}, {"x", 0}, {"y", 0}, {"params", QJsonObject{{"W", "20u"}, {"L", "1u"}}}},
                QJsonObject{{"type", "NMOS"}, {"id", "M2"}, {"x", 100}, {"y", 0}, {"params", QJsonObject{{"W", "20u"}, {"L", "1u"}}}},
                QJsonObject{{"type", "CurrentSource"}, {"id", "Itail"}, {"x", 50}, {"y", 100}, {"params", QJsonObject{{"I", "100u"}}}}
            };
            t.wires = QJsonArray{
                // M1.Source -> M2.Source (common source node)
                QJsonObject{{"from", "M1.S"}, {"to", "M2.S"}},
                // Tail current source to common source
                QJsonObject{{"from", "Itail.+"}, {"to", "M1.S"}}
            };
            t.ports = {"M1.G", "M2.G", "M1.D", "M2.D", "Itail.-"}; // Vin+, Vin-, Vout+, Vout-, GND
            map["differential_pair"] = t;
        }
        
        // ====== CMOS INVERTER ======
        // Basic digital logic gate
        {
            CircuitTemplate t;
            t.name = "inverter";
            t.description = "CMOS Inverter - Digital NOT gate";
            t.components = QJsonArray{
                QJsonObject{{"type", "PMOS"}, {"id", "MP"}, {"x", 0}, {"y", 0}, {"params", QJsonObject{{"W", "2u"}, {"L", "180n"}}}},
                QJsonObject{{"type", "NMOS"}, {"id", "MN"}, {"x", 0}, {"y", 100}, {"params", QJsonObject{{"W", "1u"}, {"L", "180n"}}}}
            };
            t.wires = QJsonArray{
                // Gates tied together
                QJsonObject{{"from", "MP.G"}, {"to", "MN.G"}},
                // Drains tied together (output)
                QJsonObject{{"from", "MP.D"}, {"to", "MN.D"}}
            };
            t.ports = {"MP.G", "MP.D", "MP.S", "MN.S"}; // Vin, Vout, VDD, GND
            map["inverter"] = t;
        }
        
        // ====== SOURCE FOLLOWER ======
        // Unity-gain buffer with current drive
        {
            CircuitTemplate t;
            t.name = "source_follower";
            t.description = "Source Follower (NMOS) - Unity gain buffer";
            t.components = QJsonArray{
                QJsonObject{{"type", "NMOS"}, {"id", "M1"}, {"x", 0}, {"y", 0}, {"params", QJsonObject{{"W", "50u"}, {"L", "1u"}}}},
                QJsonObject{{"type", "CurrentSource"}, {"id", "Ibias"}, {"x", 0}, {"y", 100}, {"params", QJsonObject{{"I", "200u"}}}}
            };
            t.wires = QJsonArray{
                // M1.Source -> Ibias
                QJsonObject{{"from", "M1.S"}, {"to", "Ibias.+"}}
            };
            t.ports = {"M1.G", "M1.S", "M1.D", "Ibias.-"}; // Vin, Vout, VDD, GND
            map["source_follower"] = t;
        }
        
        // ====== ULN2003A DARLINGTON ARRAY ======
        // 7-channel Darlington driver IC (DIP-16)
        {
            CircuitTemplate t;
            t.name = "uln2003a";
            t.description = "ULN2003A - 7-Channel Darlington Driver IC (System Integration)";
            
            // Single IC package component for schematic representation
            // Internal circuitry is expanded during netlist generation
            t.components = QJsonArray{
                QJsonObject{
                    {"type", "ICPackage"}, 
                    {"id", "U1"}, 
                    {"x", 0}, {"y", 0}, 
                    {"value", "ULN2003A"},
                    {"params", QJsonObject{{"Model", "ULN2003A"}, {"PinCount", "16"}}}
                }
            };
            
            // No explicit wires - IC package has integrated connections
            t.wires = QJsonArray{};
            
            // External ports map to IC pins
            t.ports = {"U1.1", "U1.2", "U1.3", "U1.4", "U1.5", "U1.6", "U1.7", // Inputs 1B-7B
                       "U1.8", // GND
                       "U1.9", // COM (flyback common)
                       "U1.10", "U1.11", "U1.12", "U1.13", "U1.14", "U1.15", "U1.16"}; // Outputs
            
            map["uln2003a"] = t;
        }
        
        return map;
    }
};

} // namespace AIAPI
