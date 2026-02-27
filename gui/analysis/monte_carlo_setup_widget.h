#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QHeaderView>
#include "engine_api/simulation_dto.h"
#include "../../../compute/orchestration/dto/schematic_dto.h"

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
    explicit MonteCarloSetupWidget(const acutesim::compute::orchestration::HierarchicalSchematicDTO& hierDto, QWidget* parent = nullptr);

    int numRuns() const;
    int targetNode() const;
    std::vector<acutesim::compute::orchestration::MonteCarloVariationDTO> variations() const;

private:
    void setupUI();
    void populateTable();

    const acutesim::compute::orchestration::HierarchicalSchematicDTO& hierDto_;
    QTableWidget* table_;
    QSpinBox* spinRuns_;
    QComboBox* comboStrategy_;
    QComboBox* comboTarget_;
};

} // namespace acutesim::gui::analysis
