#include "auto_harness.h"
#include "../../physics/circuitsim.h"
#include <QDebug>

namespace AIAPI {

AutoValidator::AutoValidator(QObject* parent) : QObject(parent) {}

AutoValidator::ValidationResult AutoValidator::validateBlock(const QJsonObject& blockDef, 
                                                           const QVector<Stimulus>& stimuli) {
    // 1. Create a headless scene
    GridScene harnessScene;
    harnessScene.setMode(AppMode::BENCH_TEST); // Validation happens in Bench Mode!

    // 2. Setup the test harness
    setupHarness(&harnessScene, blockDef, stimuli);

    // 3. Run Simulation
    // TODO: Ideally we'd use a HeadlessSimulator class if available, 
    // but for now we might reuse SchematicEditor's logic or call the backend directly.
    // Assuming backend exist: CircuitSimulator::runTransient(...)
    
    // MOCK IMPLEMENTATION FOR NOW until backend link is fully established
    // In real flow:
    // auto result = CircuitSimulator::runTransient(&harnessScene, 0.01, 1e-4);
    
    ValidationResult result;
    result.passed = true;
    result.report = "Simulation ran successfully (Mock)";
    
    // Simulate some trace data
    result.traceData["time"] = QJsonArray({0.0, 0.1, 0.2});

    // Capture Harness State so User can see it (Visible Harness)
    result.harnessState = harnessScene.getCircuitState();
    
    return result;
}

void AutoValidator::setupHarness(GridScene* scene, const QJsonObject& blockDef, const QVector<Stimulus>& stimuli) {
    // 1. Instantiate the Block (using its components)
    // Simplified: Just re-create the components from the definition
    QJsonArray components = blockDef["components"].toArray();
    
    QMap<QString, QPointF> portLocations;
    
    // Place Block Internals
    // NOTE: In a real implementation we would instantiate the SUB-CIRCUIT itself.
    // Here we flatten it for the harness.
    for (const auto& cVal : components) {
        QJsonObject c = cVal.toObject();
        QString typeStr = c["type"].toString();
        // Skip Ports in the instantiation, we connect to them
        if (typeStr == "PowerPort") {
            portLocations[c["name"].toString()] = QPointF(c["x"].toDouble(), c["y"].toDouble());
            continue;
        }

        auto* comp = new VisualComponent(stringToComponentType(typeStr));
        comp->setPos(c["x"].toDouble(), c["y"].toDouble());
        comp->setRotation(c["rotation"].toDouble());
        scene->addItem(comp);
    }
    
    // 2. Connect Stimuli to Input Ports
    for (const auto& stim : stimuli) {
        if (portLocations.contains(stim.portName)) {
            QPointF portPos = portLocations[stim.portName];
            addStimulusSource(scene, portPos, stim);
        }
    }
    
    // 3. Connect Probes to Output Ports (USER REQUEST: Use Probe System)
    // Probes allow us to see what's happening
    for (auto it = portLocations.begin(); it != portLocations.end(); ++it) {
        // If it's not in stimuli, treat as output/observable
        bool isInput = false;
        for(const auto& s : stimuli) if(s.portName == it.key()) isInput = true;
        
        if (!isInput) {
            addProbe(scene, it.value(), it.key());
        }
    }
}

void AutoValidator::addStimulusSource(GridScene* scene, QPointF pos, const Stimulus& stim) {
    // Create Ideal Voltage Source
    auto* src = new VisualComponent(ComponentType::VoltageSource);
    // Place it slightly to the left of the port
    src->setPos(pos.x() - 100, pos.y()); 
    scene->addItem(src);
    
    // Wire it up
    QVector<QPointF> wirePoints;
    wirePoints << QPointF(pos.x() - 100 + 40, pos.y()) << pos; // Src (+) to Port
    scene->placeWirePath(wirePoints);
    
    // Connect Ground to Source (-)
    auto* gnd = new VisualComponent(ComponentType::Ground);
    gnd->setPos(pos.x() - 100, pos.y() + 60);
    scene->addItem(gnd);
    
    QVector<QPointF> gndWire;
    gndWire << QPointF(pos.x() - 100 - 40, pos.y()) // Src (-)
            << QPointF(pos.x() - 100 - 40, pos.y() + 60)
            << QPointF(pos.x() - 100, pos.y() + 60); // GND terminal
    scene->placeWirePath(gndWire);
    
    // Set Params
    if (stim.type == "PULSE") {
        src->params["function"] = "PULSE";
        src->params["v1"] = QString::number(stim.params.value("v_low", 0));
        src->params["v2"] = QString::number(stim.params.value("v_high", 5));
    }
}

void AutoValidator::addProbe(GridScene* scene, QPointF pos, const QString& portName) {
    // Create Probe Component
    auto* probe = new VisualComponent(ComponentType::Probe);
    probe->setPos(pos.x() + 100, pos.y() - 100); // Place probe body nearby
    probe->name = "Probe_" + portName;
    scene->addItem(probe);
    
    // Move the Probe Tip to the target position
    // Note: Probe tips are children, we need to set their position RELATIVE to body
    // Tip is usually at body center initially.
    // If ProbeTip is exposed:
    if (probe->probePositiveTip) {
        probe->probePositiveTip->setPos(probe->mapFromScene(pos));
    }
}

bool AutoValidator::analyzeResults(const QJsonObject& trace, QString& report) {
    // Placeholder logic
    return true;
}

} // namespace AIAPI
