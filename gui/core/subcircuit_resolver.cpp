#include "subcircuit_resolver.h"
#include <unordered_map>
#include <string>

namespace acutesim {
namespace gui {
namespace core {

// Static Alias Map (Declarative)
static const std::unordered_map<std::string, std::string> ALIAS_MAP = {
    {"CMOS Inverter", "inverter"},  // Maps "CMOS Inverter" -> "inverter" (which will check inverter.sch)
    {"Inverter", "inverter"},
    {"INV", "inverter"},
    {"Darlington Pair", "darlington_pair"},
    {"BJT Pair", "darlington_pair"}, 
    {"Resistive Divider (10k:10k)", "resistive_divider"},
    {"Half-Wave Rectifier", "half_wave_rectifier"},
    {"RC Low-Pass Filter", "rc_low_pass"},
    {"BJT Differential Pair", "differential_pair"},
    {"BiCMOS Buffer", "bicmos_buffer"},
    {"PDK_NFET", "pdk_nfet"},
    {"PDK_PFET", "pdk_pfet"},
    {"PDK_Resistor_Poly", "pdk_res_poly"},
    {"PDK_Capacitor_MIM", "pdk_cap_mim"},
    {"ALU Core", "test_alu"}        // Alias for file-based test
};

QString SubcircuitResolver::resolveAlias(const QString& name) {
    auto it = ALIAS_MAP.find(name.toStdString());
    if (it != ALIAS_MAP.end()) {
        return QString::fromStdString(it->second);
    }
    return "";
}

ResolvedSubcircuit SubcircuitResolver::resolve(const QString& name, const QString& contextDir) {
    ResolvedSubcircuit result;
    
    if (name.isEmpty()) return result;

    QStringList attempts;
    attempts << name;
    
    // 0. Pre-process: Check aliases
    QString alias = resolveAlias(name);
    if (!alias.isEmpty()) {
        attempts << alias;
    }

    // Common search directories for filesystem checks
    QStringList searchDirs;
    if (!contextDir.isEmpty()) searchDirs << contextDir;
    searchDirs << "resources/cells" << "user_libs" << "libraries";

    for (const QString& currentName : attempts) {
        
        // 1. Filesystem Check (.sch)
        // Check exact path first
        if (QFileInfo::exists(currentName)) {
            result.source = ResolvedSubcircuit::Source::FileSystem;
            result.filePath = QFileInfo(currentName).absoluteFilePath();
            return result;
        }

        // Check relative to search directories
        QString nameWithExt = currentName.endsWith(".sch") ? currentName : currentName + ".sch";
        for (const QString& dirPath : searchDirs) {
            QDir dir(dirPath);
            QString absPath = dir.absoluteFilePath(nameWithExt);
            if (QFileInfo::exists(absPath)) {
                result.source = ResolvedSubcircuit::Source::FileSystem;
                result.filePath = absPath;
                return result;
            }
        }

        // 2. Standard Cell Library Lookup
        // Convert to std::string for lookup
        const auto* def = StandardCells::StandardCellLibrary::instance().get(currentName.toStdString());
        if (def) {
            result.source = ResolvedSubcircuit::Source::StandardLibrary;
            result.stdCellDef = def;
            return result;
        }
    }

    return result;
}

} // namespace core
} // namespace gui
} // namespace acutesim
