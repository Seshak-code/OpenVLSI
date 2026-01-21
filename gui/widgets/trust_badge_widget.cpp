#include "trust_badge_widget.h"
#include <QToolTip>
#include <QEvent>
#include <QHelpEvent>

TrustBadgeWidget::TrustBadgeWidget(QWidget* parent) : QWidget(parent) {
    setupUi();
    reset();
}

void TrustBadgeWidget::setupUi() {
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(6);
    
    iconLabel_ = new QLabel(this);
    iconLabel_->setFixedSize(16, 16);
    layout->addWidget(iconLabel_);
    
    summaryLabel_ = new QLabel(this);
    summaryLabel_->setStyleSheet("color: #ccc; font-size: 11px;");
    layout->addWidget(summaryLabel_);
    
    layout->addStretch();
    
    setToolTip("Hover for solver provenance details");
}

QString TrustBadgeWidget::getIconForLevel(TrustLevel level) const {
    switch (level) {
        case TrustLevel::HIGH:    return "✅";
        case TrustLevel::MEDIUM:  return "⚠️";
        case TrustLevel::LOW:     return "❌";
        case TrustLevel::UNKNOWN: return "❓";
    }
    return "❓";
}

QString TrustBadgeWidget::getStyleForLevel(TrustLevel level) const {
    switch (level) {
        case TrustLevel::HIGH:    return "background: #1e3a1e; border: 1px solid #2d5a2d; border-radius: 4px;";
        case TrustLevel::MEDIUM:  return "background: #3a3a1e; border: 1px solid #5a5a2d; border-radius: 4px;";
        case TrustLevel::LOW:     return "background: #3a1e1e; border: 1px solid #5a2d2d; border-radius: 4px;";
        case TrustLevel::UNKNOWN: return "background: #2d2d2d; border: 1px solid #444; border-radius: 4px;";
    }
    return "";
}

void TrustBadgeWidget::setTrustData(TrustLevel level,
                                    const QString& method,
                                    int iterations,
                                    double maxResidual,
                                    const QString& provenance) {
    iconLabel_->setText(getIconForLevel(level));
    setStyleSheet(getStyleForLevel(level));
    
    // Format residual in engineering notation
    QString residualStr;
    if (maxResidual < 1e-12) {
        residualStr = QString::number(maxResidual * 1e15, 'f', 2) + " fA";
    } else if (maxResidual < 1e-9) {
        residualStr = QString::number(maxResidual * 1e12, 'f', 2) + " pA";
    } else if (maxResidual < 1e-6) {
        residualStr = QString::number(maxResidual * 1e9, 'f', 2) + " nA";
    } else {
        residualStr = QString::number(maxResidual * 1e6, 'f', 2) + " µA";
    }
    
    QString summary = QString("%1 | %2 iter | ε = %3")
        .arg(method)
        .arg(iterations)
        .arg(residualStr);
    
    summaryLabel_->setText(summary);
    fullProvenance_ = provenance;
    
    setToolTip(provenance);
}

void TrustBadgeWidget::reset() {
    iconLabel_->setText("❓");
    summaryLabel_->setText("Not yet simulated");
    fullProvenance_.clear();
    setStyleSheet(getStyleForLevel(TrustLevel::UNKNOWN));
}

bool TrustBadgeWidget::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        QHelpEvent* helpEvent = static_cast<QHelpEvent*>(e);
        if (!fullProvenance_.isEmpty()) {
            QToolTip::showText(helpEvent->globalPos(), fullProvenance_, this);
        }
        return true;
    }
    return QWidget::event(e);
}
