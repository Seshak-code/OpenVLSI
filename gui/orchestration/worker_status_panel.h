#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QPushButton>

#include "worker_manager.h"

namespace acutesim::gui::orchestration {

class WorkerStatusPanel : public QWidget {
    Q_OBJECT
    
public:
    explicit WorkerStatusPanel(QWidget* parent = nullptr);
    ~WorkerStatusPanel();

private slots:
    void updateUI(const WorkerState& state);
    void appendLog(const QString& line, bool isError);
    void onReconnectClicked();

private:
    QLabel* m_statusLabel;
    QLabel* m_providerLabel;
    QProgressBar* m_cpuBar;
    QTextEdit* m_logView;
    QPushButton* m_reconnectBtn;
};

} // namespace acutesim::gui::orchestration
