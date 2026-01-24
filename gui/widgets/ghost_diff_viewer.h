#ifndef GHOST_DIFF_VIEWER_H
#define GHOST_DIFF_VIEWER_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace acutesim {
namespace gui {
namespace widgets {

/**
 * @brief Viewer for AI Code/HDL suggestions using visual diffs.
 * 
 * Renders added lines in Green and removed lines in Red (Strikethrough).
 * Interactions are anchored to AST nodes or line numbers.
 */
class GhostDiffViewer : public QWidget {
    Q_OBJECT

public:
    explicit GhostDiffViewer(QWidget *parent = nullptr);

    // Set the diff content (Original vs Proposed)
    void setDiff(const QString& original, const QString& proposed, const QString& contextHeader = "");

    // Clear content
    void clear();

signals:
    void accepted();
    void rejected();

private slots:
    void onAcceptClicked();
    void onRejectClicked();

private:
    void renderDiff(const QString& original, const QString& proposed);
    
    QPlainTextEdit* editor_;
    QPushButton* acceptBtn_;
    QPushButton* rejectBtn_;
    
    QString header_;
};

} // namespace widgets
} // namespace gui
} // namespace acutesim

#endif // GHOST_DIFF_VIEWER_H
