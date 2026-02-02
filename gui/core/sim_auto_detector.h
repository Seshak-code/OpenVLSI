#pragma once
#include <QString>
#include <QObject>
#include <QMap>


namespace acutesim::gui::core {

enum class SimulationType {
    None,
    Analog_SPICE,
    Digital_Verilog,
    Mixed_Signal,
    Script_Execution
};

struct SimulationContext {
    SimulationType type = SimulationType::None;
    QString mainSourceFile; // The file being simulated (or top module)
    QStringList associatedFiles; // E.g. testbench, data files
    bool isOverride = false; // If true, user manually selected this
    
    bool isValid() const { return type != SimulationType::None && !mainSourceFile.isEmpty(); }
};

class SimAutoDetector : public QObject {
    Q_OBJECT
public:
    explicit SimAutoDetector(QObject* parent = nullptr);

    // Detect context for a given file
    SimulationContext detect(const QString& filePath, const QString& content = QString());
    
    // Remember user's choice for a file
    void setOverride(const QString& filePath, SimulationType type);
    
    // Clear override
    void clearOverride(const QString& filePath);

    static QString typeToString(SimulationType type);

private:
    QMap<QString, SimulationType> overrides_;
    
    SimulationType detectFromExtension(const QString& filePath);
    SimulationType detectFromContent(const QString& content);
};

} // namespace acutesim::gui::core
