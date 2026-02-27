#include "pdk_manager.h"
#include <QFile>
#include <QDebug>
#include <QJsonArray>

// Helper to check if a value is valid based on rules
bool ParamRule::isValid(const QVariant& val) const {
    bool ok = false;
    double d = val.toDouble(&ok);
    if (!ok) return true; // Non-numeric check skipped for now (or implement string enums)
    
    if (min > -1e8 && d < min) return false;
    if (max < 1e8 && d > max) return false;
    
    // Step check could go here (fmod)
    return true;
}

PDKManager& PDKManager::instance() {
    static PDKManager instance;
    return instance;
}

void PDKManager::reload() {
    if (!currentPdkPath_.isEmpty()) {
        loadPDK(currentPdkPath_);
    }
}

bool PDKManager::loadPDK(const QString& jsonPath) {
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "PDKManager: Failed to open" << jsonPath;
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "PDKManager: Invalid JSON in" << jsonPath;
        return false;
    }
    
    PDKRules newRules;
    if (parseRules(doc.object(), newRules)) {
        currentRules_ = newRules;
        currentPdkPath_ = jsonPath;
        qDebug() << "PDKManager: Loaded" << newRules.name;
        return true;
    }
    return false;
}

bool PDKManager::parseRules(const QJsonObject& root, PDKRules& rules) {
    rules.name = root["name"].toString("Unknown PDK");
    
    // Parse Device Classes
    QJsonObject classes = root["device_classes"].toObject();
    for (auto it = classes.begin(); it != classes.end(); ++it) {
        rules.deviceClasses[it.key()] = it.value().toString();
    }
    
    // Parse Param Rules
    QJsonObject paramSection = root["param_rules"].toObject();
    for (auto it = paramSection.begin(); it != paramSection.end(); ++it) {
        QString model = it.key();
        QJsonObject modelParams = it.value().toObject();
        
        QMap<QString, ParamRule> modelRules;
        for (auto pIt = modelParams.begin(); pIt != modelParams.end(); ++pIt) {
            QString paramName = pIt.key();
            QJsonObject ruleObj = pIt.value().toObject();
            
            ParamRule rule;
            if (ruleObj.contains("min")) rule.min = ruleObj["min"].toDouble();
            if (ruleObj.contains("max")) rule.max = ruleObj["max"].toDouble();
            if (ruleObj.contains("step")) rule.step = ruleObj["step"].toDouble();
            if (ruleObj.contains("units")) rule.units = ruleObj["units"].toString();
            if (ruleObj.contains("default")) rule.defaultValue = ruleObj["default"].toVariant();
            
            modelRules[paramName] = rule;
        }
        rules.paramRules[model] = modelRules;
    }
    
    return true;
}
