#pragma once

#include <QObject>
#include <QSettings>
#include <QMainWindow>
#include <QSplitter>
#include <QByteArray>

namespace acutesim {
namespace gui {
namespace core {

class LayoutManager : public QObject {
    Q_OBJECT
public:
    static LayoutManager& instance();

    // Window State Persistence
    void saveWindowLayout(const QString& key, QMainWindow* window);
    bool restoreWindowLayout(const QString& key, QMainWindow* window);

    // Splitter Persistence
    void saveSplitterState(const QString& key, QSplitter* splitter);
    bool restoreSplitterState(const QString& key, QSplitter* splitter);

    // Global Reset
    void resetToFactory();

    // Accessors for raw data (for testing)
    QByteArray getSavedState(const QString& key) const;

private:
    LayoutManager();
    ~LayoutManager() override = default;
    LayoutManager(const LayoutManager&) = delete;
    LayoutManager& operator=(const LayoutManager&) = delete;

    QSettings settings_;
};

} // namespace core
} // namespace gui
} // namespace acutesim
