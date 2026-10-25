#include "mcp/server.h"

#include <QJsonDocument>

namespace mcp {

namespace {

const QStringList kVersions = {"2025-06-18", "2025-03-26", "2024-11-05"};  // newest first

QByteArray line(const QJsonObject& o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

QJsonObject error(const QJsonValue& id, int code, const QString& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QJsonObject result(const QJsonValue& id, const QJsonObject& r) { return {{"jsonrpc", "2.0"}, {"id", id}, {"result", r}}; }

QString text(const QJsonValue& v) {
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray()) return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    if (v.isString()) return v.toString();
    const QByteArray a = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(a.mid(1, a.size() - 2));
}

}  // namespace

std::optional<QByteArray> Server::handle(const QByteArray& in) {
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(in, &pe);
    if (pe.error != QJsonParseError::NoError) return line(error(QJsonValue::Null, -32700, "parse error: " + pe.errorString()));
    if (!doc.isObject() || doc.object()["jsonrpc"] != "2.0" || !doc.object()["method"].isString())
        return line(error(doc.isObject() ? doc.object().value("id") : QJsonValue(QJsonValue::Null), -32600, "not a JSON-RPC 2.0 request"));
    const QJsonObject req = doc.object();
    const QString method = req["method"].toString();
    const QJsonObject params = req["params"].toObject();
    if (!req.contains("id")) return std::nullopt;  // a notification (notifications/initialized, cancelled, ...)
    const QJsonValue id = req["id"];

    if (method == "initialize") {
        const QString asked = params["protocolVersion"].toString();
        return line(result(id, {{"protocolVersion", kVersions.contains(asked) ? asked : kVersions.first()},
                                {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
                                {"serverInfo", QJsonObject{{"name", "openvlsi"}, {"version", "2.0"}}},
                                {"instructions", "OpenVLSI design suite: every tool's commands. Libraries, cells and "
                                                 "views live in the workspace's database; changes are undoable."}}));
    }
    if (method == "ping") return line(result(id, {}));
    if (method == "tools/list") {
        QJsonArray tools;
        for (const auto* c : registry_.commands())
            tools.append(QJsonObject{{"name", c->name}, {"description", c->description}, {"inputSchema", c->params},
                                     {"annotations", QJsonObject{{"readOnlyHint", c->readOnly}}}});
        return line(result(id, {{"tools", tools}}));
    }
    if (method == "tools/call") {
        const QString name = params["name"].toString();
        if (!registry_.find(name)) return line(error(id, -32602, "unknown tool: " + name));
        const cmd::Result r = registry_.call(ctx_, name, params["arguments"].toObject());
        return line(result(id, {{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", r.ok ? text(r.value) : r.error}}}},
                                {"isError", !r.ok}}));
    }
    return line(error(id, -32601, "method not found: " + method));
}

}  // namespace mcp
