#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include "../../physics/statistical_engine.h"
#include "../../components/circuit.h"

namespace acutesim::gui::analysis {

/**
 * MonteCarloSetupWidget - Advanced configuration for statistical runs
 *
 * Allows users to choose:
 * - Number of iterations
 * - Sampling method (Random vs LHS)
 * - Specific components and their tolerances
 *
 * Used inside InlineDialog::showCustom() instead of as a standalone QDialog.
 */
class MonteCarloSetupWidget : public QWidget {
    Q_OBJECT
public:
    explicit MonteCarloSetupWidget(const TensorNetlist& netlist, QWidget* parent = nullptr);

    int numRuns() const;
    int targetNode() const;
    Statistics::SamplingStrategy strategy() const;
    std::map<std::string, Statistics::Distribution> variations() const;

private:
    void setupUI();
    void populateTable();

    const TensorNetlist& netlist_;
    QTableWidget* table_;
    QSpinBox* spinRuns_;
    QComboBox* comboStrategy_;
    QComboBox* comboTarget_;
};

} // namespace acutesim::gui::analysis
