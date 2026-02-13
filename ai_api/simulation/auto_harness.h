#pragma once

#include <QObject>
#include <QJsonObject>
#include <QVector>
#include <QPointer>
#include <QMap>
#include "../../gui/components/visual_component.h"
#include "../../gui/editors/schematic/schematic_editor.h"

namespace AIAPI {

/**
 * Auto-Validation Harness for Circuit Blocks.
 * 
 * Usage:
 *  1. AI/User invokes validateBlock(definition, stimuli)
 *  2. Harness creates a temporary GridScene (headless or visible)
 *  3. Instantiates the Block-Under-Test (BUT)
 *  4. Stitches Stimulus Sources to Input Ports
 *  5. Stitches Probes to Output Ports
 *  6. Runs Transient Simulation
 *  7. Returns Trace + Pass/Fail Report
 */
class AutoValidator : public QObject {
    Q_OBJECT
public:
    struct Stimulus {
        QString portName;
        QString type; // "PULSE", "SINE", "DC"
        QMap<QString, double> params; // {v_low, v_high, freq...}
    };

    struct ValidationResult {
        bool passed;
        QString report;
        QJsonObject traceData; // Waveforms
        QJsonObject harnessState; // Visual testbench
    };

    explicit AutoValidator(QObject* parent = nullptr);

    /**
     * Run validation on a Block Definition (JSON).
     */
    ValidationResult validateBlock(const QJsonObject& blockDef, 
                                 const QVector<Stimulus>& stimuli);

private:
    void setupHarness(GridScene* scene, const QJsonObject& blockDef, const QVector<Stimulus>& stimuli);
    void addStimulusSource(GridScene* scene, QPointF pos, const Stimulus& stim);
    void addProbe(GridScene* scene, QPointF pos, const QString& portName);
    
    // Check if probe results match expected logic (basic High/Low check for now)
    bool analyzeResults(const QJsonObject& trace, QString& report);
};

} // namespace AIAPI
