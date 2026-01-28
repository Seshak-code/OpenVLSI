#pragma once

#include <QString>
#include <QMap>
#include <QVariant>
#include <QJsonObject>
#include <QJsonDocument>
#include <memory>

class PDKManager;

struct ParamRule {
    double min = -1e9;
    double max = 1e9;
    double step = 0.0;
    QString units;
    QVariant defaultValue;
    
    bool isValid(const QVariant& val) const;
};

struct PDKRules {
    QString name;
    // model_name -> class (e.g. "nfet_01v8" -> "MOSFET")
    QMap<QString, QString> deviceClasses;
    // model_name -> (param_name -> rule)
    QMap<QString, QMap<QString, ParamRule>> paramRules;
    
    QString getDeviceClass(const QString& model) const {
        return deviceClasses.value(model, "UNKNOWN");
    }
};

class PDKManager {
public:
    static PDKManager& instance();
    
    // Lifecycle
    bool loadPDK(const QString& jsonPath);
    void reload();
    
    // Accessors
    const PDKRules& getPDK() const { return currentRules_; }
    QString activePdkPath() const { return currentPdkPath_; }

private:
    PDKManager() = default;
    ~PDKManager() = default;
    PDKManager(const PDKManager&) = delete;
    PDKManager& operator=(const PDKManager&) = delete;

    QString currentPdkPath_;
    PDKRules currentRules_;
    
    bool parseRules(const QJsonObject& root, PDKRules& outRules);
};
