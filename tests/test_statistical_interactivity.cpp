#include <QtTest>
#include <QSignalSpy>
#include <QPushButton>
#include "gui/analysis/statistical_dashboard.h"
#include "physics/statistical_engine.h"

class TestStatisticalInteractivity : public QObject {
    Q_OBJECT

private slots:
    void testYieldHistogram() {
        YieldHistogramWidget widget;
        std::vector<double> results = {0.9, 1.0, 1.1, 0.95, 1.05};
        widget.setData(results, 0.95, 1.05);

        // Just ensure it paints without crashing
        widget.resize(400, 300);
        widget.show();
        QTest::qWaitForWindowExposed(&widget);
    }

    void testSensitivityParetoSorting() {
        SensitivityParetoWidget widget;
        std::vector<SensitivityDisplayItem> items = {
            {"R1", "resistance", 0.5},
            {"R2", "resistance", 1.0}, // Should be first
            {"C1", "capacitance", 0.2}
        };
        widget.setData(items);
        
        widget.resize(400, 300);
        widget.show();
        QTest::qWaitForWindowExposed(&widget);
        
        // We can't easily verify the internal sorted vector without friend access,
        // but we can verify the widget doesn't crash on paint.
    }

    void testSensitivityParetoClick() {
        SensitivityParetoWidget widget;
        std::vector<SensitivityDisplayItem> items = {
            {"R_Target", "resistance", 1.0},
            {"R_Other", "resistance", 0.1}
        };
        widget.setData(items);
        widget.resize(400, 300);
        widget.show();
        QTest::qWaitForWindowExposed(&widget);

        QSignalSpy spy(&widget, &SensitivityParetoWidget::componentClicked);

        // Click near the top where the first bar (R_Target) should be.
        // Top margin is 30, ySpacing > 0.
        // Let's click at y=40 (30 + 10) which should be well within the first item.
        QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(100, 40));

        QCOMPARE(spy.count(), 1);
        QList<QVariant> arguments = spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), QString("R_Target"));
    }

    void testDashboardExportSignal() {
        StatisticalDashboard dashboard;
        dashboard.show();
        QTest::qWaitForWindowExposed(&dashboard);

        QSignalSpy spy(&dashboard, &StatisticalDashboard::exportRequested);

        // Find the "Export CSV" button
        QList<QPushButton*> buttons = dashboard.findChildren<QPushButton*>();
        QPushButton* exportBtn = nullptr;
        for (auto* btn : buttons) {
            if (btn->text() == "Export CSV") {
                exportBtn = btn;
                break;
            }
        }

        QVERIFY2(exportBtn != nullptr, "Could not find Export CSV button");

        QTest::mouseClick(exportBtn, Qt::LeftButton);

        QCOMPARE(spy.count(), 1);
    }

    void testSamplingStrategy() {
        using namespace Statistics;
        
        // Define variations
        std::map<std::string, Distribution> variations;
        variations["R1.resistance"] = Distribution::Uniform(90.0, 110.0);
        variations["C1.capacitance"] = Distribution::Gaussian(1e-6, 1e-7); // 1uF +/- 0.1uF sigma

        int runs = 100;

        // 1. Test Random Strategy
        auto scenariosRandom = MonteCarloRunner::generateScenarios(variations, runs, SamplingStrategy::RANDOM);
        QCOMPARE(scenariosRandom.size(), runs);
        QCOMPARE(scenariosRandom[0].size(), 2); // 2 parameters
        
        // Verify ranges roughly
        for(const auto& s : scenariosRandom[0]) {
             if (s.componentName == "R1") {
                 QVERIFY(s.value >= 90.0 && s.value <= 110.0);
             }
        }

        // 2. Test LHS Strategy
        auto scenariosLHS = MonteCarloRunner::generateScenarios(variations, runs, SamplingStrategy::LATIN_HYPERCUBE);
        QCOMPARE(scenariosLHS.size(), runs);
        QCOMPARE(scenariosLHS[0].size(), 2);
    }
};

QTEST_MAIN(TestStatisticalInteractivity)
#include "test_statistical_interactivity.moc"
