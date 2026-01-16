#pragma once
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>

/**
 * circuit_templates.h - Predefined Circuit Topology Templates
 * 
 * Contains JSON descriptions of common analog building blocks
 * that can be instantiated by the AI copilot.
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
    static CircuitTemplate getTemplate(const QString& name) {
        static QMap<QString, CircuitTemplate> templates = initTemplates();
        return templates.value(name.toLower());
    }
    
    static QStringList availableTemplates() {
        return {"darlington", "current_mirror", "differential_pair", "inverter", "source_follower", "uln2003a"};
    }

private:
    static QMap<QString, CircuitTemplate> initTemplates() {
        QMap<QString, CircuitTemplate> map;
        
        // ====== DARLINGTON PAIR ======
        // Complete Darlington amplifier with biasing
        // V1 (10V) -> R1 (100Ω) -> Q1/Q2 Collectors
        // I1 (1µA) -> Q1 Base, Q1 Emitter -> Q2 Base, Q2 Emitter -> GND
        {
            CircuitTemplate t;
            t.name = "darlington";
            t.description = "Darlington Pair (NPN) - Complete amplifier with biasing";
            
            // Component Layout (Grid: 20px step)
            // V1 at (0, 100), I1 at (100, 200), Ground at (200, 340)
            // R1 at (300, 60) [ROTATED 90 DEG - Vertical], Q1 at (260, 140), Q2 at (300, 240)
            t.components = QJsonArray{
                // Power & Biasing
                QJsonObject{{"type", "VoltageSource"}, {"id", "V1"}, {"x", 0}, {"y", 100}, {"value", "10"}},
                QJsonObject{{"type", "CurrentSource"}, {"id", "I1"}, {"x", 100}, {"y", 200}, {"value", "1m"}},
                QJsonObject{{"type", "Ground"}, {"id", "GND"}, {"x", 200}, {"y", 340}},
                // Load (Rotated Vertical)
                QJsonObject{{"type", "Resistor"}, {"id", "R1"}, {"x", 300}, {"y", 60}, {"value", "100"}, {"rotation", 90}},
                // Transistors
                QJsonObject{{"type", "BJT_NPN"}, {"id", "Q1"}, {"x", 260}, {"y", 140}},
                QJsonObject{{"type", "BJT_NPN"}, {"id", "Q2"}, {"x", 300}, {"y", 240}}
            };
            
            // Wiring (described as coordinate paths for L-shape routing)
            // Wire placement uses absolute coordinates matching the component terminals
            // R1 (Vertical) at (300, 60). Unrotated terminals (-40, 0), (40, 0).
            // Rotated 90: (0, -40), (0, 40) relative to (300, 60).
            // Top Terminal: (300, 20). Bottom Terminal: (300, 100).
            t.wires = QJsonArray{
                // V1+ (0, 60) -> up to (0, 0) -> right to (300, 0) -> down to R1 Top (300, 20)
                QJsonObject{{"points", QJsonArray{QJsonArray{0, 60}, QJsonArray{0, 0}, QJsonArray{300, 0}, QJsonArray{300, 20}}}},
                
                // R1 Bottom (300, 100) -> Q1.C (280, 120) and Q2.C (320, 220) junction
                QJsonObject{{"points", QJsonArray{QJsonArray{300, 100}, QJsonArray{300, 120}, QJsonArray{280, 120}}}},  // R1 -> Q1.C
                QJsonObject{{"points", QJsonArray{QJsonArray{300, 120}, QJsonArray{320, 120}, QJsonArray{320, 220}}}}, // Junction -> Q2.C
                
                // Q1.E (280, 160) -> Q2.B (280, 240)
                QJsonObject{{"points", QJsonArray{QJsonArray{280, 160}, QJsonArray{280, 240}}}},
                
                // Q2.E (320, 260) -> Ground (200, 320)
                QJsonObject{{"points", QJsonArray{QJsonArray{320, 260}, QJsonArray{320, 320}, QJsonArray{200, 320}}}},
                
                // Ground -> V1-
                QJsonObject{{"points", QJsonArray{QJsonArray{200, 320}, QJsonArray{0, 320}, QJsonArray{0, 140}}}},
                // Ground -> I1-
                QJsonObject{{"points", QJsonArray{QJsonArray{200, 320}, QJsonArray{100, 320}, QJsonArray{100, 240}}}},
                
                // I1+ (100, 160) -> Q1.B (240, 140)
                QJsonObject{{"points", QJsonArray{QJsonArray{100, 160}, QJsonArray{100, 140}, QJsonArray{240, 140}}}}
            };
            
            t.ports = {"V1.+", "GND", "Q1.B"}; // VCC, GND, Input
            map["darlington"] = t;
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
