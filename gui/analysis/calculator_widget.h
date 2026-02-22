#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QGridLayout>
#include "../../../compute/infrastructure/postproc_engine.h"

namespace acutesim::gui::analysis {

class CalculatorWidget : public QWidget {
    Q_OBJECT
public:
    explicit CalculatorWidget(QWidget* parent = nullptr);

    // Feed waveforms from simulation results
    void setWaveforms(const QMap<QString, acutesim::compute::postproc::Waveform>& waves);

signals:
    void expressionEvaluated(const QString& expr, const acutesim::compute::postproc::Operand& result);

private slots:
    void onWaveSelected();
    void onBtnClicked();
    void onEvalClicked();

private:
    QLineEdit* exprInput_;
    QListWidget* waveList_;
    acutesim::compute::postproc::PostProcEngine engine_;
    
    void setupUI();
    QPushButton* createBtn(const QString& text, const QString& insertText = "");
};

} // namespace acutesim::gui::analysis
