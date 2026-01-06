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
        return {"darlington", "current_mirror", "differential_pair", "inverter", "source_follower"};
    }

private:
    static QMap<QString, CircuitTemplate> initTemplates() {
        QMap<QString, CircuitTemplate> map;
        
        // ====== DARLINGTON PAIR ======
        // Two NPN BJTs in cascade for high current gain (β₁ × β₂)
        {
            CircuitTemplate t;
            t.name = "darlington";
            t.description = "Darlington Pair (NPN) - High current gain configuration";
            t.components = QJsonArray{
                QJsonObject{{"type", "BJT_NPN"}, {"id", "Q1"}, {"x", 0}, {"y", 0}},
                QJsonObject{{"type", "BJT_NPN"}, {"id", "Q2"}, {"x", 0}, {"y", 100}}
            };
            t.wires = QJsonArray{
                // Q1.Emitter -> Q2.Base
                QJsonObject{{"from", "Q1.E"}, {"to", "Q2.B"}},
                // Q1.Collector -> Q2.Collector (tied together)
                QJsonObject{{"from", "Q1.C"}, {"to", "Q2.C"}}
            };
            t.ports = {"Q1.B", "Q2.E", "Q2.C"}; // Input, Output, Collector
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
        
        return map;
    }
};

} // namespace AIAPI
