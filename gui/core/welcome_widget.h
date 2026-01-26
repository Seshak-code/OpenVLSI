#pragma once

#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QDateTime>
#include "gui_types.h"

class WelcomeWidget : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeWidget(QWidget* parent = nullptr);
    void addRecentProject(const QString& path, const QString& name);

signals:
    // DEPRECATED: Mode parameter ignored - unified component library
    // Signal always emits BENCH_TEST for backwards compatibility
    void newSchematicRequested(AppMode mode);

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
