#include "pdk_manager.h"
#include <QDir>
#include <QDebug>
#ifndef __EMSCRIPTEN__
#include <QProcess>
#endif

namespace acutesim::gui::orchestration {

PdkManager& PdkManager::instance() {
    static PdkManager inst;
    return inst;
}

PdkManager::PdkManager(QObject* parent) : QObject(parent) {
    // Static catalogue of known PDKs
    knownPDKs_ = {
        {"sky130",   "SkyWater 130nm (sky130)",  "1.0", false, ""},
        {"gf180mcu", "GlobalFoundries 180nm MCU (gf180mcu)", "1.0", false, ""},
        {"ihp-sg13g2","IHP 130nm SiGe BiCMOS (sg13g2)", "1.0", false, ""},
    };
}

QList<PDKInfo> PdkManager::availablePDKs() const {
    return knownPDKs_;
}

QList<PDKInfo> PdkManager::installedPDKs() const {
    QList<PDKInfo> installed;
    for (const auto& p : knownPDKs_) {
        if (p.isInstalled) installed << p;
    }
    return installed;
}

QString PdkManager::pdkPath(const QString& pdkId) const {
    for (const auto& p : knownPDKs_) {
        if (p.id == pdkId && p.isInstalled) return p.installedPath;
    }
    return {};
}

void PdkManager::updateFromInventory(const QString& pdkRoot, const QStringList& installedNames) {
    pdkRoot_ = pdkRoot;
    for (auto& pdk : knownPDKs_) {
        bool found = installedNames.contains(pdk.id, Qt::CaseInsensitive);
        pdk.isInstalled = found;
        if (found && !pdkRoot.isEmpty()) {
            pdk.installedPath = QDir(pdkRoot).filePath(pdk.id);
        }
    }
    emit pdkListChanged();
}

void PdkManager::requestDownload(const QString& pdkId) {
#ifdef __EMSCRIPTEN__
    // WASM: route via gRPC (not yet implemented — emit error for now)
    emit downloadError(pdkId, "PDK download on WASM requires a connected cloud worker.");
#else
    qDebug() << "[PdkManager] Download requested for" << pdkId
             << "(implement via worker gRPC or volare in future)";
    emit downloadError(pdkId, "Direct download not yet implemented. "
                              "Install via 'volare enable " + pdkId + "' or set $PDK_ROOT.");
#endif
}

} // namespace acutesim::gui::orchestration
