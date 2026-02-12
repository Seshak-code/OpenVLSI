#include "pnr_result_dialog.h"
#include <QFormLayout>
#include <QHeaderView>

namespace acutesim::gui::orchestration {

namespace {

QLabel* makeValueLabel(const QString& text, QWidget* parent = nullptr) {
    auto* lbl = new QLabel(text, parent);
    lbl->setStyleSheet("font-size: 13px; font-weight: bold; color: #4ec9b0;");
    return lbl;
}

QProgressBar* makeUtilBar(double value, QWidget* parent = nullptr) {
    auto* bar = new QProgressBar(parent);
    bar->setRange(0, 100);
    bar->setValue(static_cast<int>(value * 100));
    bar->setTextVisible(true);
    bar->setFormat(QString::number(value * 100, 'f', 1) + "%");
    bar->setFixedHeight(20);
    bar->setStyleSheet(
        "QProgressBar { background: #333; border: 1px solid #444; border-radius: 3px; color: #ccc; }"
        "QProgressBar::chunk { background: #0e639c; border-radius: 2px; }");
    return bar;
}

} // anonymous namespace

PnRResultDialog::PnRResultDialog(
    const acutesim::compute::orchestration::PnRResult& result,
    QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Place & Route Results");
    setMinimumSize(520, 440);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* mainLayout = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);

    // ---- Tab 1: Placement ----
    {
        auto* tab = new QWidget();
        auto* form = new QFormLayout(tab);
        form->setLabelAlignment(Qt::AlignRight);

        form->addRow("Total Area:", makeValueLabel(
            QString::number(result.placement.totalArea, 'f', 2) + " um\u00B2"));
        form->addRow("Utilization:", makeUtilBar(result.placement.utilization, tab));
        form->addRow("Instances:", makeValueLabel(QString::number(result.placement.instanceCount)));
        form->addRow("Unplaced:", makeValueLabel(QString::number(result.placement.unplacedCount)));
        tabs->addTab(tab, "Placement");
    }

    // ---- Tab 2: Timing ----
    {
        auto* tab = new QWidget();
        auto* layout = new QVBoxLayout(tab);

        auto* indicator = new acutesim::gui::widgets::TimingIndicatorWidget(tab);
        indicator->setTiming(result.timing.wns, result.timing.tns,
                             result.timing.setupViolations, result.timing.holdViolations);
        indicator->setMinimumHeight(100);
        layout->addWidget(indicator);

        auto* table = new QTableWidget(2, 2, tab);
        table->setHorizontalHeaderLabels({"Metric", "Value"});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setStyleSheet("QTableWidget { background: #2d2d2d; color: #ccc; }");
        table->setItem(0, 0, new QTableWidgetItem("Setup Violations"));
        table->setItem(0, 1, new QTableWidgetItem(QString::number(result.timing.setupViolations)));
        table->setItem(1, 0, new QTableWidgetItem("Hold Violations"));
        table->setItem(1, 1, new QTableWidgetItem(QString::number(result.timing.holdViolations)));
        layout->addWidget(table);

        tabs->addTab(tab, "Timing");
    }

    // ---- Tab 3: Routing ----
    {
        auto* tab = new QWidget();
        auto* form = new QFormLayout(tab);
        form->setLabelAlignment(Qt::AlignRight);

        int totalNets = result.routing.routedNets + result.routing.unroutedNets;
        double routedFraction = totalNets > 0 ?
            static_cast<double>(result.routing.routedNets) / totalNets : 1.0;

        form->addRow("Routed Nets:", makeValueLabel(
            QString("%1 / %2").arg(result.routing.routedNets).arg(totalNets)));
        form->addRow("Completion:", makeUtilBar(routedFraction, tab));
        form->addRow("DRC Violations:", makeValueLabel(QString::number(result.routing.drcViolations)));
        form->addRow("Antenna Violations:", makeValueLabel(QString::number(result.routing.antennaViolations)));
        tabs->addTab(tab, "Routing");
    }

    // ---- Tab 4: Power ----
    {
        auto* tab = new QWidget();
        auto* form = new QFormLayout(tab);
        form->setLabelAlignment(Qt::AlignRight);

        form->addRow("Total Power:", makeValueLabel(
            QString::number(result.power.totalPower, 'f', 3) + " mW"));
        form->addRow("Dynamic:", makeValueLabel(
            QString::number(result.power.dynamicPower, 'f', 3) + " mW"));
        form->addRow("Leakage:", makeValueLabel(
            QString::number(result.power.leakagePower, 'f', 3) + " mW"));
        form->addRow("Internal:", makeValueLabel(
            QString::number(result.power.internalPower, 'f', 3) + " mW"));

        // Stacked bar (simple horizontal representation)
        double total = result.power.totalPower > 0 ? result.power.totalPower : 1.0;
        auto* barLayout = new QHBoxLayout();
        auto makeSegment = [&](double val, const QColor& color) {
            auto* seg = new QWidget(tab);
            int pct = static_cast<int>(100.0 * val / total);
            seg->setMinimumHeight(20);
            seg->setMinimumWidth(std::max(pct * 2, 2));
            seg->setStyleSheet(QString("background: %1; border-radius: 2px;").arg(color.name()));
            return seg;
        };
        barLayout->setSpacing(2);
        barLayout->addWidget(makeSegment(result.power.dynamicPower, QColor("#2196F3")));
        barLayout->addWidget(makeSegment(result.power.leakagePower, QColor("#FF9800")));
        barLayout->addWidget(makeSegment(result.power.internalPower, QColor("#9C27B0")));
        barLayout->addStretch();
        form->addRow("Breakdown:", barLayout);

        tabs->addTab(tab, "Power");
    }

    // ---- Tab 5: Log ----
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
