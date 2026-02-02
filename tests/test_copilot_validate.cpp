#include <QTest>
#include <QSignalSpy>
#include <QTextEdit>
#include <QLineEdit>
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
        // setMode() removed - unified component library

        // Add a simple circuit (Probe In + Resistor + Probe Out)
        editor.placeComponent("Probe", 0, 0, "PinIn", R"({"value": "Vin"})");
        editor.placeComponent("Resistor", 50, 0, "R1", R"({"value": "1k"})");
        editor.placeComponent("Probe", 100, 0, "PinOut", R"({"value": "Vout"})");

        // 2. Setup Assistant Panel
        AiAssistantPanel panel(&editor);

        // Access private members via findChild or by checking side effects?
        // Easier to check output in the QTextEdit if we can access it.
        // AiAssistantPanel has `chatHistory` as a member.
        // We will fallback to "findChild" approach since members are private
        QTextEdit* history = panel.findChild<QTextEdit*>();
        QVERIFY(history != nullptr);

        // 3. Execute "/validate" (Auto mode)
        // We can call processCommand via invoking the slot or direct call if public.
        // It's private/protected usually.
        // BUT `onSendMessage` is a slot triggered by the button.
        // We can simulate input.

        QLineEdit* input = panel.findChild<QLineEdit*>();
        QVERIFY(input != nullptr);

        // Test: Validation runs without mode restrictions
        input->setText("/validate");
        QMetaObject::invokeMethod(&panel, "onSendMessage");
        
        // Since validateComponent is synchronous in our mock implementation (see schema_api.cpp),
        // we should see results immediately.
        QString log = history->toPlainText();
        qDebug() << "Log:" << log;
        
        // We expect "Running validation..." and "Validation Passed" (since our mock AutoValidator returns pass=true)
        QVERIFY(log.contains("Running validation"));
        QVERIFY(log.contains("Validation Passed"));
        
        // Test C: Custom JSON
        input->setText("/validate {\"stimuli\": []}");
        QMetaObject::invokeMethod(&panel, "onSendMessage");
        log = history->toPlainText();
        QVERIFY(log.contains("Validation Passed"));
    }
};

QTEST_MAIN(CopilotValidateTest)
#include "test_copilot_validate.moc"
