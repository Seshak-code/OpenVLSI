#pragma once
#include <QDialog>
#include <QTableWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDialogButtonBox>
#include "../../physics/statistical_engine.h"
#include "../../components/circuit.h"

namespace acutesim::gui::analysis {

/**
 * MonteCarloSetupDialog - Advanced configuration for statistical runs
 * 
 * Allows users to choose:
 * - Number of iterations
 * - Sampling method (Random vs LHS)
 * - Specific components and their tolerances
 */
class MonteCarloSetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit MonteCarloSetupDialog(const TensorNetlist& netlist, QWidget* parent = nullptr);
    
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
