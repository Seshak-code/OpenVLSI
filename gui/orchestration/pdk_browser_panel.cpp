#include "pdk_browser_panel.h"
#include "pdk_manager.h"
#include "gui/widgets/inline_dialog.h"
#include "gui/widgets/toast_manager.h"
#include <QHBoxLayout>
#include <QListWidgetItem>

namespace acutesim::gui::orchestration {

PdkBrowserPanel::PdkBrowserPanel(QWidget* parent) : QWidget(parent) {
    buildUi();
    connectSignals();
    refresh();
}

void PdkBrowserPanel::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* titleLabel = new QLabel("PDK Browser", this);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 11px;");
    layout->addWidget(titleLabel);

    pdkList_ = new QListWidget(this);
    pdkList_->setAlternatingRowColors(false);
    pdkList_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(pdkList_, 1);

    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setVisible(false);
    progressBar_->setFixedHeight(6);
    layout->addWidget(progressBar_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    auto* btnRow = new QHBoxLayout();
    downloadBtn_ = new QPushButton("Download", this);
    configureBtn_ = new QPushButton("Configure Path", this);
    btnRow->addWidget(downloadBtn_);
    btnRow->addWidget(configureBtn_);
    layout->addLayout(btnRow);
}

void PdkBrowserPanel::connectSignals() {
    auto& pm = PdkManager::instance();

    connect(&pm, &PdkManager::pdkListChanged, this, &PdkBrowserPanel::refresh);

    connect(&pm, &PdkManager::downloadProgress, this,
        [this](const QString& id, int pct) {
            if (id == pendingDownload_) {
                progressBar_->setValue(pct);
                progressBar_->setVisible(true);
            }
        });

    connect(&pm, &PdkManager::downloadComplete, this,
        [this](const QString& id, const QString& path) {
            if (id == pendingDownload_) {
                progressBar_->setVisible(false);
                pendingDownload_.clear();
                using namespace acutesim::gui::widgets;
                ToastManager::instance().showToast(
                    QString("PDK '%1' installed at %2").arg(id, path), ToastLevel::Success);
            }
            refresh();
        });

    connect(&pm, &PdkManager::downloadError, this,
        [this](const QString& id, const QString& err) {
            if (id == pendingDownload_) {
                progressBar_->setVisible(false);
                pendingDownload_.clear();
                using namespace acutesim::gui::widgets;
                InlineDialog::critical(this, "PDK Download Failed", err);
            }
        });

    connect(downloadBtn_, &QPushButton::clicked, this, &PdkBrowserPanel::onDownloadClicked);
    connect(configureBtn_, &QPushButton::clicked, this, &PdkBrowserPanel::onConfigureClicked);
}

void PdkBrowserPanel::refresh() {
    pdkList_->clear();
    const auto pdks = PdkManager::instance().availablePDKs();
    for (const auto& pdk : pdks) {
        auto* item = new QListWidgetItem(pdkList_);
        QString indicator = pdk.isInstalled ? "✓ " : "  ";
        item->setText(indicator + pdk.displayName);
        item->setData(Qt::UserRole, pdk.id);
        if (pdk.isInstalled) {
            item->setForeground(QColor("#4CAF50"));
            item->setToolTip(pdk.installedPath);
        }
    }

    statusLabel_->setText(
        QString("%1 of %2 PDKs installed")
            .arg(PdkManager::instance().installedPDKs().size())
            .arg(pdks.size()));
}

void PdkBrowserPanel::onDownloadClicked() {
    auto* item = pdkList_->currentItem();
    if (!item) return;

    QString pdkId = item->data(Qt::UserRole).toString();
    using namespace acutesim::gui::widgets;

    InlineDialog::question(this, "Download PDK",
        QString("Download and install PDK '%1'?").arg(pdkId),
        DialogButton::Ok | DialogButton::Cancel,
        [this, pdkId](const DialogResult& r) {
            if (r.clickedButton != DialogButton::Ok) return;
            pendingDownload_ = pdkId;
            progressBar_->setValue(0);
            progressBar_->setVisible(true);
            PdkManager::instance().requestDownload(pdkId);
        });
}

void PdkBrowserPanel::onConfigureClicked() {
    using namespace acutesim::gui::widgets;
    InlineDialog::getText(this, "Configure PDK Path",
        "Enter the path to your PDK root directory:", "",
        [](const DialogResult& r) {
            if (r.clickedButton == DialogButton::Ok && !r.textValue.isEmpty()) {
                PdkManager::instance().updateFromInventory(r.textValue, {});
                ToastManager::instance().showToast(
                    "PDK path updated. Rescanning...", ToastLevel::Info);
            }
        });
}

} // namespace acutesim::gui::orchestration
