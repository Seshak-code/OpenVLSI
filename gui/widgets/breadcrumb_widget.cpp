#include <optional>
#include "breadcrumb_widget.h"
#include <QStyle>

namespace acutesim::gui::widgets {

BreadcrumbWidget::BreadcrumbWidget(QWidget* parent)
    : QWidget(parent)
{
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(5, 2, 5, 2);
    layout_->setSpacing(2);

    // Back button (always present, hidden when at root)
    backButton_ = new QToolButton(this);
    backButton_->setText("<");
    backButton_->setToolTip("Navigate to parent schematic");
    backButton_->setStyleSheet(
        "QToolButton { "
        "  background: transparent; "
        "  color: #569CD6; "
        "  border: none; "
        "  padding: 2px 6px; "
        "  font-weight: bold; "
        "} "
        "QToolButton:hover { "
        "  background: #3A3A3A; "
        "  border-radius: 3px; "
        "}"
    );
    connect(backButton_, &QToolButton::clicked, this, [this]() {
        if (hierarchyPath_ && !hierarchyPath_->isAtRoot()) {
            emit navigationRequested(hierarchyPath_->depth() - 1);
        }
    });
    layout_->addWidget(backButton_);

    layout_->addStretch();

    // Start hidden (shown only when hierarchy depth > 0)
    setVisible(false);

    setStyleSheet("background-color: #2D2D2D; border-bottom: 1px solid #3A3A3A;");
    setFixedHeight(28);
}

void BreadcrumbWidget::setHierarchyPath(acutesim::gui::core::HierarchyPath* path) {
    if (hierarchyPath_) {
        disconnect(hierarchyPath_, nullptr, this, nullptr);
    }

    hierarchyPath_ = path;

    if (hierarchyPath_) {
        connect(hierarchyPath_, &acutesim::gui::core::HierarchyPath::levelChanged,
                this, &BreadcrumbWidget::onLevelChanged);
        connect(hierarchyPath_, &acutesim::gui::core::HierarchyPath::cleared,
                this, &BreadcrumbWidget::onCleared);
        rebuild();
    } else {
        clearSegments();
        updateVisibility();
    }
}

void BreadcrumbWidget::rebuild() {
    clearSegments();

    if (!hierarchyPath_ || hierarchyPath_->levels().isEmpty()) {
        updateVisibility();
        return;
    }

    const auto& levels = hierarchyPath_->levels();

    for (int i = 0; i < levels.size(); ++i) {
        // Add separator before non-root elements
        if (i > 0) {
            QLabel* sep = createSeparator();
            layout_->insertWidget(layout_->count() - 1, sep);  // Before stretch
            segments_.append(sep);
        }

        QToolButton* btn = createSegmentButton(levels[i].displayName, i);
        layout_->insertWidget(layout_->count() - 1, btn);  // Before stretch
        segments_.append(btn);

        // Highlight current level (last in list)
        if (i == levels.size() - 1) {
            btn->setStyleSheet(
                "QToolButton { "
                "  background: transparent; "
                "  color: #FFFFFF; "
                "  border: none; "
                "  padding: 2px 8px; "
                "  font-weight: bold; "
                "}"
            );
        }
    }

    updateVisibility();
}

void BreadcrumbWidget::clearSegments() {
    for (QWidget* w : segments_) {
        layout_->removeWidget(w);
        w->deleteLater();
    }
    segments_.clear();
}

QToolButton* BreadcrumbWidget::createSegmentButton(const QString& text, int depth) {
    QToolButton* btn = new QToolButton(this);
    btn->setText(text);
    btn->setToolTip(QString("Navigate to %1").arg(text));
    btn->setStyleSheet(
        "QToolButton { "
        "  background: transparent; "
        "  color: #AAAAAA; "
        "  border: none; "
        "  padding: 2px 8px; "
        "} "
        "QToolButton:hover { "
        "  background: #3A3A3A; "
        "  border-radius: 3px; "
        "  color: #FFFFFF; "
        "}"
    );

    connect(btn, &QToolButton::clicked, this, [this, depth]() {
        emit navigationRequested(depth);
    });

    return btn;
}

QLabel* BreadcrumbWidget::createSeparator() {
    QLabel* sep = new QLabel(">", this);
    sep->setStyleSheet("color: #666666; padding: 0 2px;");
    return sep;
}

void BreadcrumbWidget::updateVisibility() {
    // Show only when we have hierarchy depth > 0 (not at root)
    bool shouldShow = hierarchyPath_ && !hierarchyPath_->isAtRoot();
    setVisible(shouldShow);

    // Back button enabled only when not at root
    if (backButton_) {
        backButton_->setEnabled(shouldShow);
    }
}

void BreadcrumbWidget::onLevelChanged(const acutesim::gui::core::HierarchyLevel& /*newLevel*/,
                                       std::optional<acutesim::gui::core::HierarchyLevel> /*previous*/) {
    rebuild();
}

void BreadcrumbWidget::onCleared() {
    clearSegments();
    updateVisibility();
}

} // namespace acutesim::gui::widgets
