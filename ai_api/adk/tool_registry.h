#pragma once

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <functional>

namespace AIAPI {

/**
 * ToolParameter - Describes a single parameter for an ADK tool.
 */
struct ToolParameter {
    QString name;
    QString type;           // "string", "number", "integer", "boolean", "object", "array"
    QString description;
    bool required = false;
    QJsonValue defaultValue;
    QJsonArray enumValues;  // If non-empty, parameter is constrained to these values
};

/**
 * ToolDefinition - Describes an ADK-compatible tool.
 * Maps to Google ADK FunctionDeclaration schema.
 */
struct ToolDefinition {
    QString name;
    QString description;
    QVector<ToolParameter> parameters;

    /**
     * Generate JSON Schema for this tool's parameters.
     */
    QJsonObject toJsonSchema() const;

    /**
     * Generate ADK-compatible manifest entry.
     */
    QJsonObject toManifest() const;
};

/**
 * ToolResult - Result of invoking a tool.
 */
struct ToolResult {
    bool success = false;
    QString errorMessage;
    QJsonObject data;

    static ToolResult ok(const QJsonObject& data) {
        return {true, {}, data};
    }

    static ToolResult error(const QString& msg) {
        return {false, msg, {}};
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["success"] = success;
        if (!errorMessage.isEmpty()) obj["error"] = errorMessage;
        if (!data.isEmpty()) obj["data"] = data;
        return obj;
    }
};

/**
 * ToolHandler - Function type for tool implementations.
 */
using ToolHandler = std::function<ToolResult(const QJsonObject& params)>;

/**
 * ToolRegistry - Singleton registry for ADK-compatible tools.
 *
 * Registers builtin tools with JSON Schema definitions, validates
 * parameters, and dispatches invocations to handler functions.
 */
class ToolRegistry {
public:
    static ToolRegistry& instance();

    /**
     * Register a tool with its definition and handler.
     */
    void registerTool(const ToolDefinition& definition, ToolHandler handler);

    /**
     * Invoke a tool by name with parameters.
     */
    ToolResult invoke(const QString& toolName, const QJsonObject& params);

    /**
     * Get the full tool manifest (all registered tools).
     * Compatible with Google ADK tools format.
     */
    QJsonArray getToolManifest() const;

    /**
     * Validate parameters against a tool's schema.
     */
    bool validateParams(const QString& toolName, const QJsonObject& params, QString& error) const;

    /**
     * Check if a tool is registered.
     */
    bool hasTool(const QString& name) const;

    /**
     * Get list of all registered tool names.
     */
    QStringList toolNames() const;

    /**
     * Get a specific tool definition.
     */
    ToolDefinition getToolDefinition(const QString& name) const;

private:
    ToolRegistry();
    void registerBuiltinTools();

    mutable QMutex mutex_;
    QMap<QString, ToolDefinition> definitions_;
    QMap<QString, ToolHandler> handlers_;
};

} // namespace AIAPI
