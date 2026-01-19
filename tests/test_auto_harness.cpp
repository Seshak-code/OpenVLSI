#include <QTest>
#include <QSignalSpy>
#include <QDebug>
#include "../gui/analysis/erc_engine.h"
#include "../ai_api/simulation/auto_harness.h"
#include "../gui/components/visual_component.h"
#include "../gui/editors/schematic_editor.h"

// Mock for testing without full GUI
class AutoHarnessTest : public QObject {
    Q_OBJECT

private slots:
    // ========================================================================
    // 1. ARC ENGINE RULES (Strong Rules Test)
    // ========================================================================
    
    void testBlockModeRestrictions() {
        GridScene scene;
        ERC::ERCEngine erc;
        
        // Setup: BLOCK Mode
        erc.setMode(AppMode::BLOCK_SUBSYSTEM);
        
        // Case A: Valid Block (Ports + Components)
        auto* r1 = new VisualComponent(ComponentType::Resistor);
        auto* pIn = new VisualComponent(ComponentType::PowerPort); // "Vin"
        auto* pOut = new VisualComponent(ComponentType::PowerPort); // "Vout"
        scene.addItem(r1);
        scene.addItem(pIn);
        scene.addItem(pOut);
        
        QList<ERC::Violation> v = erc.runFullCheck(&scene);
        
        // Should have NO "Illegal Source" errors
        // Might have disconnected/floating errors, but logic check is for Block Rules
        bool hasIllegalSource = false;
        for(const auto& err : v) {
            if (err.type == ERC::ViolationType::ILLEGAL_SOURCE) hasIllegalSource = true;
        }
        QVERIFY2(!hasIllegalSource, "Valid Block should not have ILLEGAL_SOURCE violation");
        
        // Case B: Invalid Block (Contains Voltage Sources)
        auto* vsrc = new VisualComponent(ComponentType::VoltageSource);
        scene.addItem(vsrc);
        
        v = erc.runFullCheck(&scene);
        hasIllegalSource = false;
        for(const auto& err : v) {
            if (err.type == ERC::ViolationType::ILLEGAL_SOURCE) hasIllegalSource = true;
        }
        QVERIFY2(hasIllegalSource, "Block with independent source MUST trigger ILLEGAL_SOURCE violation");
    }
    
    void testSystemModeRestrictions() {
        GridScene scene;
        ERC::ERCEngine erc;
        erc.setMode(AppMode::SYSTEM_INTEGRATION);
        
        // Case A: Invalid System (Generic Sources)
        auto* vsrc = new VisualComponent(ComponentType::VoltageSource);
        scene.addItem(vsrc);
        
        QList<ERC::Violation> v = erc.runFullCheck(&scene);
        bool hasIllegalDriver = false;
        for(const auto& err : v) {
            if (err.type == ERC::ViolationType::ILLEGAL_DRIVER) hasIllegalDriver = true;
        }
        QVERIFY2(hasIllegalDriver, "System with generic source MUST trigger ILLEGAL_DRIVER violation");
        
        // Case B: Valid System (RailDriver)
        scene.removeItem(vsrc);
        delete vsrc;
        auto* bat = new VisualComponent(ComponentType::RailDriver);
        scene.addItem(bat);
        
        v = erc.runFullCheck(&scene);
        hasIllegalDriver = false;
        for(const auto& err : v) {
            if (err.type == ERC::ViolationType::ILLEGAL_DRIVER) hasIllegalDriver = true;
        }
        QVERIFY2(!hasIllegalDriver, "System with RailDriver should be valid");
    }

    // ========================================================================
    // 2. AUTO-VALIDATOR (Harness Synthesis)
    // ========================================================================

    void testAnalogHarnessGeneration() {
        // Define a simple RC Block Definition
        QJsonObject blockDef;
        QJsonArray comps;
        
        // Input Port
        QJsonObject pin; pin["type"] = "PowerPort"; pin["name"] = "In"; pin["x"] = 0; pin["y"] = 0;
        comps.append(pin);
        
        // Resistor
        QJsonObject r; r["type"] = "Resistor"; r["x"] = 20; r["y"] = 0; r["rotation"] = 0;
        comps.append(r);
        
        // Output Port
        QJsonObject pout; pout["type"] = "PowerPort"; pout["name"] = "Out"; pout["x"] = 40; pout["y"] = 0;
        comps.append(pout);
        blockDef["components"] = comps;
        
        // Stimulus (Analog Sine)
        QVector<AIAPI::AutoValidator::Stimulus> stimuli;
        AIAPI::AutoValidator::Stimulus stim;
        stim.portName = "In";
        stim.type = "PULSE"; // Simple pulse for now
        stim.params["v_low"] = 0.0;
        stim.params["v_high"] = 5.0;
        stimuli.append(stim);
        
        // Run Validator
        AIAPI::AutoValidator validator;
        auto result = validator.validateBlock(blockDef, stimuli);
        
        QVERIFY2(result.passed, "AutoValidator should pass for valid Analog block");
        QVERIFY2(!result.traceData.isEmpty(), "Validator should return trace data");
    }
    
    void testDigitalHarnessGeneration() {
        // Define an Inverter Block (P-MOS + N-MOS)
        QJsonObject blockDef;
        QJsonArray comps;

        // In Port -> Gate
        QJsonObject pin; pin["type"] = "PowerPort"; pin["name"] = "In"; pin["x"] = 0; pin["y"] = 40;
        comps.append(pin);
        
        // PMOS
        QJsonObject pmos; pmos["type"] = "Mosfet"; pmos["name"] = "M1"; pmos["x"] = 40; pmos["y"] = 20;
        comps.append(pmos);

        // NMOS
        QJsonObject nmos; nmos["type"] = "Mosfet"; nmos["name"] = "M2"; nmos["x"] = 40; nmos["y"] = 60;
        comps.append(nmos);
        
        blockDef["components"] = comps;
        
        // Stimulus (Digital Pulse)
        QVector<AIAPI::AutoValidator::Stimulus> stimuli;
        AIAPI::AutoValidator::Stimulus stim;
        stim.portName = "In";
        stim.type = "PULSE";
        stim.params["v_low"] = 0.0;
        stim.params["v_high"] = 1.8;
        stimuli.append(stim);
        
        AIAPI::AutoValidator validator;
        auto result = validator.validateBlock(blockDef, stimuli);
        
        QVERIFY2(result.passed, "AutoValidator should pass for valid Digital block");
    }
};

QTEST_MAIN(AutoHarnessTest)
#include "test_auto_harness.moc"
