#include "cmd/registry.h"

#include <QRegularExpression>

#include <cmath>
#include <exception>
#include <stdexcept>

namespace cmd {

namespace {

const QStringList kTypes = {"object", "string", "integer", "number", "boolean", "array"};

bool isType(const QJsonValue& v, const QString& t) {
    if (t == "object") return v.isObject();
    if (t == "string") return v.isString();
    if (t == "boolean") return v.isBool();
    if (t == "array") return v.isArray();
    if (t == "number") return v.isDouble();
    if (t == "integer") return v.isDouble() && std::floor(v.toDouble()) == v.toDouble() && std::isfinite(v.toDouble());
    return false;
}

// The schema subset validate() reads; anything else is a mistake in the command, caught when it is registered
void checkSchema(const QJsonObject& s, const QString& where) {
    static const QStringList keys = {"type", "description", "properties", "required", "enum", "items"};
    for (auto it = s.begin(); it != s.end(); ++it)
        if (!keys.contains(it.key())) throw std::invalid_argument((where + ": schema key '" + it.key() + "' is not supported").toStdString());
    if (s.contains("type") && !kTypes.contains(s["type"].toString()))
        throw std::invalid_argument((where + ": schema type '" + s["type"].toString() + "' is not supported").toStdString());
    const QJsonObject props = s["properties"].toObject();
    for (auto it = props.begin(); it != props.end(); ++it) checkSchema(it.value().toObject(), where + "." + it.key());
    for (const auto& r : s["required"].toArray())
        if (!props.contains(r.toString())) throw std::invalid_argument((where + ": required '" + r.toString() + "' is not a property").toStdString());
    if (s.contains("items")) checkSchema(s["items"].toObject(), where + "[]");
}

}  // namespace

QString validate(const QJsonObject& schema, const QJsonValue& value, const QString& path) {
    const QString type = schema["type"].toString();
    if (!type.isEmpty() && !isType(value, type)) return path + ": expected " + type;
    if (schema.contains("enum") && !schema["enum"].toArray().contains(value)) return path + ": not one of the allowed values";
    if (type == "object") {
        const QJsonObject o = value.toObject(), props = schema["properties"].toObject();
        for (const auto& r : schema["required"].toArray())
            if (!o.contains(r.toString())) return path + "." + r.toString() + ": required";
        const bool closed = schema.contains("properties");  // an object without listed properties is a free-form map
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (!closed) continue;
            if (!props.contains(it.key())) return path + "." + it.key() + ": unknown parameter";
            if (QString e = validate(props[it.key()].toObject(), it.value(), path + "." + it.key()); !e.isEmpty()) return e;
        }
    }
    if (type == "array" && schema.contains("items")) {
        const QJsonArray a = value.toArray();
        for (int i = 0; i < a.size(); ++i)
            if (QString e = validate(schema["items"].toObject(), a[i], path + "[" + QString::number(i) + "]"); !e.isEmpty()) return e;
    }
    return "";
}

QJsonObject param(const QString& type, const QString& description) {
    QJsonObject p{{"description", description}};
    if (!type.isEmpty()) p["type"] = type;
    return p;
}

QJsonObject object(std::initializer_list<std::pair<const char*, QJsonObject>> required,
                   std::initializer_list<std::pair<const char*, QJsonObject>> optional) {
    QJsonObject p;
    QJsonArray req;
    for (const auto& [k, s] : required) { p[k] = s; req.append(k); }
    for (const auto& [k, s] : optional) p[k] = s;
    QJsonObject o{{"type", "object"}, {"properties", p}};
    if (!req.isEmpty()) o["required"] = req;
    return o;
}

void Registry::add(Command c) {
    static const QRegularExpression name("^[a-z][a-z0-9_]{0,63}$");
    if (!name.match(c.name).hasMatch()) throw std::invalid_argument(("command name '" + c.name + "' must match [a-z][a-z0-9_]*").toStdString());
    if (commands_.count(c.name)) throw std::invalid_argument(("command '" + c.name + "' is already registered").toStdString());
    if (c.params["type"].toString() != "object") throw std::invalid_argument((c.name + ": params must be an object schema").toStdString());
    if (c.description.isEmpty() || !c.run) throw std::invalid_argument((c.name + ": needs a description and a handler").toStdString());
    checkSchema(c.params, c.name);
    const QString key = c.name;
    commands_.emplace(key, std::move(c));
}

const Command* Registry::find(const QString& name) const {
    const auto it = commands_.find(name);
    return it == commands_.end() ? nullptr : &it->second;
}

std::vector<const Command*> Registry::commands() const {
    std::vector<const Command*> out;
    for (const auto& [n, c] : commands_) out.push_back(&c);
    return out;
}

Result Registry::call(Context& ctx, const QString& name, const QJsonObject& args) {
    Result r;
    if (const Command* c = find(name); !c)
        r.error = "no command '" + name + "'";
    else if (r.error = validate(c->params, args); r.error.isEmpty()) {
        try {
            r.value = c->run(ctx, args);
            r.ok = true;
        } catch (const std::exception& e) {
            r.error = QString::fromUtf8(e.what());
        }
    }
    QJsonObject entry{{"command", name}, {"args", args}, {"ok", r.ok}};
    if (!r.ok) entry["error"] = r.error;
    log_.append(entry);
    return r;
}

}  // namespace cmd
