#include <QtTest>
#ifndef Q_MOC_RUN
#include "tool_registry.h"
#include "tool_handlers.h"
#endif

class TestAdkToolRegistry : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Register all handlers
        AIAPI::ToolHandlers::registerAll();
    }

    void testAllToolsRegistered() {
        auto& registry = AIAPI::ToolRegistry::instance();
        QStringList names = registry.toolNames();

        QVERIFY(names.contains("simulate_circuit"));
        QVERIFY(names.contains("retrieve_similar_designs"));
        QVERIFY(names.contains("get_circuit_context"));
        QVERIFY(names.contains("check_design_rules"));
        QVERIFY(names.contains("get_debug_suggestions"));
        QVERIFY(names.contains("submit_feedback"));
        QVERIFY(names.contains("get_counterfactuals"));
        QVERIFY(names.contains("compress_context"));

        QCOMPARE(names.size(), 8);
    }

    void testManifestGeneration() {
        auto& registry = AIAPI::ToolRegistry::instance();
        QJsonArray manifest = registry.getToolManifest();

        QCOMPARE(manifest.size(), 8);

        // Each entry should have name, description, parameters
        for (const auto& entry : manifest) {
            QJsonObject tool = entry.toObject();
            QVERIFY(tool.contains("name"));
            QVERIFY(tool.contains("description"));
            QVERIFY(tool.contains("parameters"));

            QJsonObject params = tool["parameters"].toObject();
            QCOMPARE(params["type"].toString(), QString("object"));
            QVERIFY(params.contains("properties"));
        }
    }

    void testSchemaValidation() {
        auto& registry = AIAPI::ToolRegistry::instance();

        // Valid params
        QJsonObject validParams;
        validParams["analysis_type"] = "DC";
        QString error;
        QVERIFY(registry.validateParams("simulate_circuit", validParams, error));

        // Missing required param
        QJsonObject emptyParams;
        QVERIFY(!registry.validateParams("simulate_circuit", emptyParams, error));
        QVERIFY(error.contains("analysis_type"));
    }

    void testInvokeSimulateCircuit() {
        auto& registry = AIAPI::ToolRegistry::instance();

        QJsonObject params;
        params["analysis_type"] = "DC";
        params["duration"] = 1e-3;

        AIAPI::ToolResult result = registry.invoke("simulate_circuit", params);
        QVERIFY(result.success);
        QCOMPARE(result.data["analysis_type"].toString(), QString("DC"));
        QCOMPARE(result.data["status"].toString(), QString("queued"));
    }

    void testInvokeGetCircuitContext() {
        auto& registry = AIAPI::ToolRegistry::instance();

        QJsonObject params;
        params["detail_level"] = "medium";
        params["max_tokens"] = 2000;

        AIAPI::ToolResult result = registry.invoke("get_circuit_context", params);
        QVERIFY(result.success);
        QVERIFY(result.data.contains("context"));
        QVERIFY(result.data.contains("budget"));
    }

    void testInvokeSubmitFeedback() {
        auto& registry = AIAPI::ToolRegistry::instance();

        QJsonObject params;
        params["query_id"] = "test-query-123";
        params["rating"] = 1;
        params["correction"] = "";

        AIAPI::ToolResult result = registry.invoke("submit_feedback", params);
        QVERIFY(result.success);
        QCOMPARE(result.data["status"].toString(), QString("recorded"));
    }

    void testInvokeCompressContext() {
        auto& registry = AIAPI::ToolRegistry::instance();

        QJsonObject context;
        QJsonObject topology;
        topology["deviceCount"] = 5;
        context["topology"] = topology;

        QJsonObject params;
        params["context_json"] = context;
        params["max_tokens"] = 500;
        params["priority_mode"] = "strict";

        AIAPI::ToolResult result = registry.invoke("compress_context", params);
        QVERIFY(result.success);
        QVERIFY(result.data.contains("budget"));
    }

    void testInvokeUnknownTool() {
        auto& registry = AIAPI::ToolRegistry::instance();

        QJsonObject params;
        AIAPI::ToolResult result = registry.invoke("nonexistent_tool", params);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains("Unknown tool"));
    }

    void testInvokeInvalidParams() {
        auto& registry = AIAPI::ToolRegistry::instance();

        // submit_feedback requires query_id and rating
        QJsonObject params;
        params["rating"] = 1;
        // Missing query_id

        AIAPI::ToolResult result = registry.invoke("submit_feedback", params);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains("query_id"));
    }

    void testToolDefinitionAccess() {
        auto& registry = AIAPI::ToolRegistry::instance();

        QVERIFY(registry.hasTool("simulate_circuit"));
        QVERIFY(!registry.hasTool("nonexistent"));

        AIAPI::ToolDefinition def = registry.getToolDefinition("simulate_circuit");
        QCOMPARE(def.name, QString("simulate_circuit"));
        QVERIFY(!def.parameters.isEmpty());
    }
};

QTEST_MAIN(TestAdkToolRegistry)
#include "test_adk_tool_registry.moc"
