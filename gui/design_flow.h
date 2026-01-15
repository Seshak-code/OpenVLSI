#pragma once

#include <QObject>

/**
 * DesignStage
 * Represents the stages in a typical ASIC design flow.
 */
enum class DesignStage {
    RTL_DESIGN,        // Verilog/Schematic entry, functional verification
    SYNTHESIS,         // Logic synthesis, timing optimization
    PHYSICAL_DESIGN,   // Netlist to layout (P&R, floorplanning)
    VERIFICATION       // DRC/LVS, sign-off checks
};

/**
 * DesignFlowManager
 * Manages the current design stage and notifies the UI when it changes.
 */
class DesignFlowManager : public QObject {
    Q_OBJECT
public:
    explicit DesignFlowManager(QObject* parent = nullptr) : QObject(parent) {}
    
    void setStage(DesignStage stage) {
        if (m_stage != stage) {
            m_stage = stage;
            emit stageChanged(stage);
        }
    }
    
    DesignStage currentStage() const { return m_stage; }
    
    static QString stageName(DesignStage stage) {
        switch (stage) {
            case DesignStage::RTL_DESIGN: return "RTL Design";
            case DesignStage::SYNTHESIS: return "Synthesis";
            case DesignStage::PHYSICAL_DESIGN: return "Physical Design";
            case DesignStage::VERIFICATION: return "Verification";
        }
        return "Unknown";
    }

signals:
    void stageChanged(DesignStage newStage);
    
private:
    DesignStage m_stage = DesignStage::RTL_DESIGN;
};
