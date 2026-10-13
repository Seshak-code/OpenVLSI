#include "welcome_widget.h"
#include <QFont>
#include <QFileInfo>

WelcomeWidget::WelcomeWidget(QWidget* parent) : QWidget(parent) {
    setStyleSheet("background-color: #1e1e1e;");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setAlignment(Qt::AlignCenter);
    mainLayout->setSpacing(20);

    // Logo / Title
    QLabel* logo = new QLabel("OpenVLSI");
    logo->setStyleSheet("color: #007ACC; font-size: 48px; font-weight: bold;");
    logo->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(logo);

    QLabel* subtitle = new QLabel("ASIC Design Suite");
    subtitle->setStyleSheet("color: #888; font-size: 18px;");
    subtitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(subtitle);

    mainLayout->addSpacing(40);

    // New Schematic Button - Single unified button (mode system removed)
    QLabel* startLabel = new QLabel("Start a new design:");
    startLabel->setStyleSheet("color: #CCC; font-size: 14px;");
    startLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(startLabel);

    QPushButton* newSchematicBtn = new QPushButton("New Schematic");
    newSchematicBtn->setToolTip("Create a new schematic with full component library access");
    newSchematicBtn->setMinimumSize(250, 60);
    newSchematicBtn->setStyleSheet(
        "QPushButton { background: #007ACC; color: white; font-size: 18px; font-weight: bold; border-radius: 8px; }"
        "QPushButton:hover { background: #0099CC; }"
    );
    connect(newSchematicBtn, &QPushButton::clicked, this, [this]() {
        emit newSchematicRequested();
    });
    mainLayout->addWidget(newSchematicBtn, 0, Qt::AlignCenter);

    mainLayout->addSpacing(30);

    // Recent Projects section
    QLabel* recentLabel = new QLabel("Recent Projects");
    recentLabel->setStyleSheet("color: #CCC; font-size: 14px; font-weight: bold;");
    recentLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(recentLabel);

    recentList_ = new QListWidget();
    recentList_->setMaximumWidth(400);
    recentList_->setMaximumHeight(200);
    recentList_->setStyleSheet(R"(
        QListWidget {
            background: #252526;
            border: 1px solid #3c3c3c;
            border-radius: 6px;
            color: #ccc;
            font-size: 12px;
            outline: none;
        }
        QListWidget::item {
            padding: 8px 12px;
            border-bottom: 1px solid #333;
        }
        QListWidget::item:hover {
            background: #2a2d2e;
        }
        QListWidget::item:selected {
            background: #094771;
        }
    )");
    connect(recentList_, &QListWidget::itemClicked, this, &WelcomeWidget::onRecentItemClicked);
    mainLayout->addWidget(recentList_, 0, Qt::AlignCenter);

    // Load persisted recent projects
    loadRecentProjects();

    mainLayout->addStretch();
}

void WelcomeWidget::loadRecentProjects() {
    QSettings settings("AcuteSim", "RecentProjects");
    int count = settings.beginReadArray("projects");
    recentProjects_.clear();

    for (int i = 0; i < count && i < MAX_RECENT; ++i) {
        settings.setArrayIndex(i);
        RecentProject proj;
        proj.path = settings.value("path").toString();
        proj.name = settings.value("name").toString();
        proj.lastOpened = settings.value("lastOpened").toDateTime();
        if (!proj.path.isEmpty()) {
            recentProjects_.append(proj);
        }
    }
    settings.endArray();

    refreshRecentProjectsList();
}

void WelcomeWidget::saveRecentProjects() {
    QSettings settings("AcuteSim", "RecentProjects");
    settings.beginWriteArray("projects");

    for (int i = 0; i < recentProjects_.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("path", recentProjects_[i].path);
        settings.setValue("name", recentProjects_[i].name);
        settings.setValue("lastOpened", recentProjects_[i].lastOpened);
    }
    settings.endArray();
}

void WelcomeWidget::addRecentProject(const QString& path, const QString& name) {
    // Remove if already exists (to move it to front)
    for (int i = 0; i < recentProjects_.size(); ++i) {
        if (recentProjects_[i].path == path) {
            recentProjects_.removeAt(i);
            break;
        }
    }

    // Add to front
    RecentProject proj;
    proj.path = path;
    proj.name = name.isEmpty() ? QFileInfo(path).baseName() : name;
    proj.lastOpened = QDateTime::currentDateTime();
    recentProjects_.prepend(proj);

    // Limit to MAX_RECENT
    while (recentProjects_.size() > MAX_RECENT) {
        recentProjects_.removeLast();
    }

    saveRecentProjects();
    refreshRecentProjectsList();
}

void WelcomeWidget::refreshRecentProjectsList() {
    recentList_->clear();

    if (recentProjects_.isEmpty()) {
        QListWidgetItem* placeholder = new QListWidgetItem("No recent projects");
        placeholder->setFlags(placeholder->flags() & ~Qt::ItemIsEnabled);
        placeholder->setForeground(QColor("#666"));
        recentList_->addItem(placeholder);
        return;
    }

    for (const RecentProject& proj : recentProjects_) {
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(QString("%1\n%2").arg(proj.name, proj.path));
        item->setData(Qt::UserRole, proj.path);
        item->setToolTip(QString("Last opened: %1").arg(proj.lastOpened.toString("yyyy-MM-dd hh:mm")));
        recentList_->addItem(item);
    }
}

void WelcomeWidget::onRecentItemClicked(QListWidgetItem* item) {
    QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty()) {
        emit openRecentProjectRequested(path);
    }
}
