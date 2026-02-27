#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QVector>
#include <QVector>
#include "engine_api/simulation_dto.h"

namespace acutesim::gui::analysis {

class CornerSetupWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit CornerSetupWidget(QWidget* parent = nullptr);
    
    // Returns the list of corners to run
    QVector<acutesim::compute::orchestration::CornerParametersDTO> getCorners() const;

signals:
    void configurationChanged();

private slots:
    void updatePreview();

private:
    // Controls
    QComboBox* m_processCombo;
    QDoubleSpinBox* m_tempMin;
    QDoubleSpinBox* m_tempMax;
    QDoubleSpinBox* m_tempStep;
    
    QDoubleSpinBox* m_voltageScale; // ±10%
    
    QCheckBox* m_monteCarloEnable;
    QSpinBox* m_monteCarloRuns;
    
    // Preview
    void setupUi();
};

} // namespace acutesim::gui::analysis
