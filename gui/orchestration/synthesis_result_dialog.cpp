#include "synthesis_result_dialog.h"
#include <QHeaderView>

namespace acutesim::gui::orchestration {

SynthesisResultDialog::SynthesisResultDialog(
    const acutesim::compute::orchestration::SynthesisResult& result,
    QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Synthesis Results");
    setMinimumSize(520, 420);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* mainLayout = new QVBoxLayout(this);

    // ---- Summary row ----
    auto* summaryLayout = new QHBoxLayout();
    auto makeStat = [](const QString& label, const QString& value) {
        auto* w = new QWidget();
        auto* l = new QVBoxLayout(w);
        l->setContentsMargins(8, 4, 8, 4);
        auto* valLbl = new QLabel(value, w);
        valLbl->setStyleSheet("font-size: 18px; font-weight: bold; color: #4ec9b0;");
        valLbl->setAlignment(Qt::AlignCenter);
        l->addWidget(valLbl);
        auto* nameLbl = new QLabel(label, w);
        nameLbl->setStyleSheet("font-size: 11px; color: #888;");
        nameLbl->setAlignment(Qt::AlignCenter);
        l->addWidget(nameLbl);
        return w;
    };

    summaryLayout->addWidget(makeStat("Cells", QString::number(result.cellCount)));
    summaryLayout->addWidget(makeStat("Wires", QString::number(result.wireCount)));
    summaryLayout->addWidget(makeStat("Area", QString::number(result.estimatedArea, 'f', 2)));
    mainLayout->addLayout(summaryLayout);

    // ---- Tabs ----
    auto* tabs = new QTabWidget(this);

    // Tab 1: Cell Breakdown
    {
        auto* tab = new QWidget();
        auto* tabLayout = new QVBoxLayout(tab);

        auto* pieChart = new acutesim::gui::widgets::CellPieChartWidget(tab);
        pieChart->setData(result.cellBreakdown);
        pieChart->setMinimumHeight(180);
        tabLayout->addWidget(pieChart);

        // Table
        auto* table = new QTableWidget(result.cellBreakdown.size(), 3, tab);
        table->setHorizontalHeaderLabels({"Cell Type", "Count", "%"});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setStyleSheet("QTableWidget { background: #2d2d2d; color: #ccc; }");

        int row = 0;
        int total = result.cellCount > 0 ? result.cellCount : 1;
        for (auto it = result.cellBreakdown.constBegin(); it != result.cellBreakdown.constEnd(); ++it) {
            table->setItem(row, 0, new QTableWidgetItem(it.key()));
            table->setItem(row, 1, new QTableWidgetItem(QString::number(it.value())));
            double pct = 100.0 * it.value() / total;
            table->setItem(row, 2, new QTableWidgetItem(QString::number(pct, 'f', 1) + "%"));
            row++;
        }
        tabLayout->addWidget(table);
        tabs->addTab(tab, "Cell Breakdown");
    }

    // Tab 2: Module Stats
    {
        auto* table = new QTableWidget(result.modules.size(), 4, this);
        table->setHorizontalHeaderLabels({"Module", "Cells", "Wires", "Area"});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setStyleSheet("QTableWidget { background: #2d2d2d; color: #ccc; }");

        int row = 0;
        for (auto it = result.modules.constBegin(); it != result.modules.constEnd(); ++it) {
            const auto& mod = it.value();
            table->setItem(row, 0, new QTableWidgetItem(mod.moduleName));
            table->setItem(row, 1, new QTableWidgetItem(QString::number(mod.cellCount)));
            table->setItem(row, 2, new QTableWidgetItem(QString::number(mod.wireCount)));
            table->setItem(row, 3, new QTableWidgetItem(QString::number(mod.chipArea, 'f', 2)));
            row++;
        }
        tabs->addTab(table, "Module Stats");
    }

    // Tab 3: Log
    {
        auto* logEdit = new QTextEdit(this);
        logEdit->setReadOnly(true);
        logEdit->setStyleSheet(
            "QTextEdit { background: #1a1a2e; color: #b0b0b0; "
            "font-family: 'Menlo', 'Consolas', monospace; font-size: 11px; }");

        for (const auto& w : result.warnings)
            logEdit->append("<span style='color:#dcdcaa;'>WARNING: " + w.toHtmlEscaped() + "</span>");
        for (const auto& e : result.errors)
            logEdit->append("<span style='color:#f44747;'>ERROR: " + e.toHtmlEscaped() + "</span>");
        if (result.warnings.isEmpty() && result.errors.isEmpty())
            logEdit->setPlainText("No warnings or errors.");
        tabs->addTab(logEdit, "Log");
    }

    mainLayout->addWidget(tabs);

    // OK button
    auto* okBtn = new QPushButton("OK", this);
    okBtn->setFixedWidth(80);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    mainLayout->addLayout(btnLayout);
}

} // namespace acutesim::gui::orchestration
