#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>

namespace acutesim::gui::orchestration {

struct PDKInfo {
    QString id;          // e.g. "sky130", "gf180mcu"
    QString displayName; // e.g. "SkyWater 130nm"
    QString version;
    bool isInstalled = false;
    QString installedPath;
};

/**
 * @brief Singleton that manages Process Design Kit (PDK) discovery and downloads.
 *
 * - Native: reads $PDK_ROOT / common paths, updated by WorkerManager::toolInventoryReady
 * - WASM:   all PDK access is server-side via gRPC; local list always empty
 */
class PdkManager : public QObject {
    Q_OBJECT
public:
    static PdkManager& instance();

    // All known PDKs (installed + not-installed)
    QList<PDKInfo> availablePDKs() const;

    // Only installed PDKs
    QList<PDKInfo> installedPDKs() const;

    // Resolved path for a given PDK id (empty if not installed)
    QString pdkPath(const QString& pdkId) const;

    // Update installation status from ToolInventory discovery
    void updateFromInventory(const QString& pdkRoot, const QStringList& installedNames);

    // Request download / installation.  On WASM, routes via gRPC.
    void requestDownload(const QString& pdkId);

signals:
    void pdkListChanged();
    void downloadProgress(const QString& pdkId, int percent);
    void downloadComplete(const QString& pdkId, const QString& path);
    void downloadError(const QString& pdkId, const QString& error);

private:
    explicit PdkManager(QObject* parent = nullptr);

    QList<PDKInfo> knownPDKs_; // Static catalogue
    QString pdkRoot_;
};

} // namespace acutesim::gui::orchestration
