#include "welcome_widget.h"
#include <QFont>

WelcomeWidget::WelcomeWidget(QWidget* parent) : QWidget(parent) {
    setStyleSheet("background-color: #1e1e1e;");
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setAlignment(Qt::AlignCenter);
    mainLayout->setSpacing(20);
    
    // Logo / Title
    QLabel* logo = new QLabel("AcuteSim");
    logo->setStyleSheet("color: #007ACC; font-size: 48px; font-weight: bold;");
    logo->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(logo);
    
    QLabel* subtitle = new QLabel("ASIC Design Suite");
    subtitle->setStyleSheet("color: #888; font-size: 18px;");
    subtitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(subtitle);
    
    mainLayout->addSpacing(40);
    
    // New Schematic Buttons
    QLabel* startLabel = new QLabel("Start a new schematic:");
    startLabel->setStyleSheet("color: #CCC; font-size: 14px;");
    startLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(startLabel);
    
    auto createButton = [this, mainLayout](const QString& text, const QString& desc, AppMode mode, const QString& color) {
        QPushButton* btn = new QPushButton(text);
        btn->setToolTip(desc);
        btn->setMinimumSize(250, 50);
        btn->setStyleSheet(QString(
            "QPushButton { background: %1; color: white; font-size: 16px; font-weight: bold; border-radius: 8px; }"
            "QPushButton:hover { background: %2; }"
        ).arg(color, color == "#2D8A2D" ? "#3DA93D" : (color == "#8A8A2D" ? "#9A9A3D" : "#0099CC")));
        connect(btn, &QPushButton::clicked, this, [this, mode]() {
            emit newSchematicRequested(mode);
        });
        mainLayout->addWidget(btn, 0, Qt::AlignCenter);
    };
    
    createButton("[T]  Bench Test", "Ideal sources, fast prototyping, implicit ground", AppMode::BENCH_TEST, "#2D8A2D");
    createButton("[B]  Block Subsystem", "Reusable blocks with PowerPort interfaces", AppMode::BLOCK_SUBSYSTEM, "#8A8A2D");
    createButton("[S]  System Integration", "Full system with realistic PDN and ICs", AppMode::SYSTEM_INTEGRATION, "#007ACC");
    
    mainLayout->addStretch();
    
    // Recent Projects (Placeholder)
    QLabel* recentLabel = new QLabel("Recent: (Coming Soon)");
    recentLabel->setStyleSheet("color: #555; font-size: 12px;");
    recentLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(recentLabel);
}
