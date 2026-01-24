#ifndef TOAST_MANAGER_H
#define TOAST_MANAGER_H

#include <QObject>
#include <QLabel>
#include <QTimer>
#include <QQueue>
#include <QWidget>

namespace acutesim {
namespace gui {
namespace widgets {

enum class ToastLevel { Info, Success, Warning, Error, Critical };

/**
 * @brief Manages non-modal notification "Toasts".
 * 
 * Replaces intrusive QMessageBox. Toasts appear at the bottom center of the
 * parent widget (usually MainWindow or SchematicEditor).
 */
class ToastManager : public QObject {
    Q_OBJECT

public:
    static ToastManager& instance();
    
    // Initialize with the root widget where toasts should render
    void setRootWidget(QWidget* root);
    
    void showToast(const QString& message, ToastLevel level = ToastLevel::Info);

private slots:
    void processQueue();
    void fadeOut();

private:
    ToastManager(QObject* parent = nullptr);
    ~ToastManager() = default;
    
    QWidget* rootWidget_ = nullptr;
    QLabel* currentToast_ = nullptr;
    QTimer* fadeTimer_;
    
    struct ToastEntry {
        QString message;
        ToastLevel level;
    };
    QQueue<ToastEntry> queue_;
    bool isShowing_ = false;
};

} // namespace widgets
} // namespace gui
} // namespace acutesim

#endif // TOAST_MANAGER_H
