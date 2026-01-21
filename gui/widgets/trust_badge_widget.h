#pragma once

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QString>

/**
 * TrustBadgeWidget - Displays solver provenance and trust indicators.
 * 
 * Part of Phase 1.7.9: Trust Surface Layer.
 * Shows the user *why* a simulation result is valid:
 * - Solver method (Newton-Raphson, GMIN Stepping)
 * - Max residual (KCL error)
 * - Iteration count
 * - Warning badges for fallback mechanisms
 */
class TrustBadgeWidget : public QWidget {
    Q_OBJECT

public:
    enum class TrustLevel {
        HIGH,       // Clean convergence
        MEDIUM,     // Converged with GMIN/Source stepping
        LOW,        // Failed or suspicious
        UNKNOWN     // Not yet run
    };

    explicit TrustBadgeWidget(QWidget* parent = nullptr);
    
    void setTrustData(TrustLevel level,
                      const QString& method,
                      int iterations,
                      double maxResidual,
                      const QString& provenance);
    
    void reset();

private:
    QLabel* iconLabel_;
    QLabel* summaryLabel_;
    QString fullProvenance_;
    
    void setupUi();
    QString getIconForLevel(TrustLevel level) const;
    QString getStyleForLevel(TrustLevel level) const;
    
protected:
    bool event(QEvent* e) override;
};
