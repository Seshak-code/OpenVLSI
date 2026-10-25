#pragma once
#include <QJsonObject>

// AI API: the open schematic's circuit state, as JSON for the AI context (ContextEngine)
namespace AIAPI {

QJsonObject getFullCircuitState();  // empty when no schematic editor is open

} // namespace AIAPI
