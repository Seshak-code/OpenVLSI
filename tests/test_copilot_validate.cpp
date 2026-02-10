#include <QTest>
#include <QSignalSpy>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include "../gui/panels/ai_assistant_panel.h"
#include "../gui/editors/schematic_editor.h"
#include "../ai_api/system/schema_api.h"

using acutesim::gui::panels::AiAssistantPanel;

class CopilotValidateTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Ensure standard paths are setup if needed
    }

    void testValidateCommand() {
        // 1. Setup Mock Editor
        SchematicEditor editor("test.sch");

        // Add a simple circuit — spread components to avoid collision
        editor.placeComponent("Probe", 0, 0, "PinIn", R"({"value": "Vin"})");
        editor.placeComponent("Resistor", 200, 0, "R1", R"({"value": "1k"})");
        editor.placeComponent("Probe", 400, 0, "PinOut", R"({"value": "Vout"})");
        editor.placeComponent("Ground", 200, 200, "GND0", R"({})");

        // 2. Setup Assistant Panel and connect editor
        AiAssistantPanel panel;
        panel.setEditor(&editor);

        // Access chat history — panel uses QTextBrowser
        QTextBrowser* history = panel.findChild<QTextBrowser*>();
        QVERIFY(history != nullptr);

        // 3. Execute "/validate" — find input by objectName to avoid ambiguity
        QPlainTextEdit* input = panel.findChild<QPlainTextEdit*>("copilot_input");
        QVERIFY(input != nullptr);

        // Test: Validation runs without mode restrictions
        input->setPlainText("/validate");
        QMetaObject::invokeMethod(&panel, "onSendClicked");

        // Since validation is synchronous, results should appear immediately.
        QString log = history->toPlainText();
        qDebug() << "Log:" << log;

        // We expect "Running validation" and "Validation Passed"
        QVERIFY(log.contains("Running validation"));
        QVERIFY(log.contains("Validation Passed"));

        // Test C: Custom JSON
        input->setPlainText("/validate {\"stimuli\": []}");
        QMetaObject::invokeMethod(&panel, "onSendClicked");
        log = history->toPlainText();
        QVERIFY(log.contains("Validation Passed"));
    }
};

QTEST_MAIN(CopilotValidateTest)
#include "test_copilot_validate.moc"
