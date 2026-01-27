#pragma once

#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QDateTime>
// gui_types.h include removed - AppMode no longer used

class WelcomeWidget : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeWidget(QWidget* parent = nullptr);
    void addRecentProject(const QString& path, const QString& name);

signals:
    // Signal to create a new schematic (mode parameter removed - unified library)
    void newSchematicRequested();

    void openRecentProjectRequested(const QString& path);

private slots:
    void onRecentItemClicked(QListWidgetItem* item);

private:
    void loadRecentProjects();
    void saveRecentProjects();
    void refreshRecentProjectsList();

    QListWidget* recentList_;

    struct RecentProject {
        QString path;
        QString name;
        QDateTime lastOpened;
    };
    QList<RecentProject> recentProjects_;
    static constexpr int MAX_RECENT = 10;
};
