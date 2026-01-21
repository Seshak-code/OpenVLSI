#include "worker_status_panel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

namespace acutesim::gui::orchestration {

WorkerStatusPanel::WorkerStatusPanel(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // Top Bar: Status Indicators
    auto* topBar = new QHBoxLayout();
    
    // Status Dot
    m_statusLabel = new QLabel("● Disconnected");
    m_statusLabel->setStyleSheet("color: #7f8c8d; font-weight: bold;"); // Gray
    
    // Provider Info
    m_providerLabel = new QLabel("Provider: None");
    
    // CPU Usage
    m_cpuBar = new QProgressBar();
    m_cpuBar->setRange(0, 100);
    m_cpuBar->setValue(0);
    m_cpuBar->setFixedWidth(100);
    m_cpuBar->setTextVisible(false);
    m_cpuBar->setToolTip("Worker CPU Load");
    
    // Reconnect Button
    m_reconnectBtn = new QPushButton("Connect");
    connect(m_reconnectBtn, &QPushButton::clicked, this, &WorkerStatusPanel::onReconnectClicked);
    
    topBar->addWidget(m_statusLabel);
    topBar->addSpacing(10);
    topBar->addWidget(m_providerLabel);
    topBar->addStretch();
    topBar->addWidget(new QLabel("CPU:"));
    topBar->addWidget(m_cpuBar);
    topBar->addWidget(m_reconnectBtn);
    
    mainLayout->addLayout(topBar);
    
    // Log Area
    m_logView = new QTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setPlaceholderText("Worker logs will appear here...");
    m_logView->setStyleSheet("font-family: monospace; background-color: #1e1e1e; color: #dcdcdc;");
    
    mainLayout->addWidget(m_logView);
    
    // Connect to Manager
    auto* manager = WorkerManager::instance();
    connect(manager, &WorkerManager::statusChanged, this, &WorkerStatusPanel::updateUI);
    connect(manager, &WorkerManager::logReceived, this, &WorkerStatusPanel::appendLog);
    
    // Trigger initial update
    updateUI(manager->currentState());
}

WorkerStatusPanel::~WorkerStatusPanel() = default;

void WorkerStatusPanel::updateUI(const WorkerState& state) {
    if (state.isConnected) {
        m_statusLabel->setText("● Connected");
        m_statusLabel->setStyleSheet("color: #27ae60; font-weight: bold;"); // Green
        m_reconnectBtn->setText("Disconnect");
        m_logView->setEnabled(true);
    } else {
        m_statusLabel->setText("● Disconnected");
        m_statusLabel->setStyleSheet("color: #c0392b; font-weight: bold;"); // Red
        m_reconnectBtn->setText("Connect");
        m_cpuBar->setValue(0);
    }
    
    m_providerLabel->setText("Provider: " + state.activeProvider);
    m_cpuBar->setValue(static_cast<int>(state.cpuUsage));
}

void WorkerStatusPanel::appendLog(const QString& line, bool isError) {
    QString color = isError ? "#e74c3c" : "#bdc3c7";
    m_logView->append(QString("<span style='color:%1'>%2</span>").arg(color, line));
}

void WorkerStatusPanel::onReconnectClicked() {
    auto* manager = WorkerManager::instance();
    if (m_reconnectBtn->text() == "Connect") {
        manager->connectToWorker(); // Use default local
    } else {
        manager->disconnect();
    }
}

} // namespace acutesim::gui::orchestration
