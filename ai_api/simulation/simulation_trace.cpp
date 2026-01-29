#include "simulation_trace.h"
#include <QStringList>

namespace AIAPI {

QString SimulationTrace::getSummary() const {
    QString summary = QString("Trace ID: %1\nTopology: %2\nTimestamp: %3\n")
                          .arg(traceId)
                          .arg(topologyHash)
                          .arg(timestamp);
    
    summary += "Device Mix: ";
    QStringList devices;
    for (auto it = deviceMix.begin(); it != deviceMix.end(); ++it) {
        devices << QString("%1: %2").arg(it.key()).arg(it.value());
    }
    summary += devices.join(", ") + "\n";
    
    if (failureModes.empty()) {
        summary += "Result: SUCCESS (Converged)\n";
    } else {
        summary += "Result: FAILED\nFailure Modes:\n";
        for (const auto& fail : failureModes) {
            summary += QString(" - %1: %2 (at t=%3)\n")
                           .arg(failureTypeToString(fail.type))
                           .arg(fail.message)
                           .arg(fail.timestamp);
        }
    }
    
    return summary;
}

} // namespace AIAPI
