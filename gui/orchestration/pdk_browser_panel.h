#pragma once

#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace acutesim::gui::orchestration {

/**
 * @brief Dockable panel showing available PDKs with install status.
 *
 * Connects to PdkManager for live updates and download progress.
 * All confirmations use InlineDialog; status via ToastManager.
 */
class PdkBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit PdkBrowserPanel(QWidget* parent = nullptr);

private slots:
    void refresh();
    void onDownloadClicked();
    void onConfigureClicked();

private:
    void buildUi();
    void connectSignals();

    QListWidget* pdkList_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* downloadBtn_ = nullptr;
    QPushButton* configureBtn_ = nullptr;
    QString pendingDownload_;
};

} // namespace acutesim::gui::orchestration
