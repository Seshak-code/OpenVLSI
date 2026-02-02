#pragma once

#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QDir>
#include <optional>
#include "../../components/standard_cells.h"

namespace acutesim {
namespace gui {
namespace core {

/**
 * @brief Result of a subcircuit resolution attempt.
 */
struct ResolvedSubcircuit {
    enum class Source {
        FileSystem,
        StandardLibrary,
        None
    };

    Source source = Source::None;
    QString filePath; // Valid if Source == FileSystem
    const SubCircuitDefinition* stdCellDef = nullptr; // Valid if Source == StandardLibrary

    bool isValid() const { return source != Source::None; }
};

/**
 * @brief Centralized resolver for subcircuits.
 * 
 * Priority:
 * 1. Explicit Filesystem Check (.sch)
 * 2. Standard Cell Library Lookup
 * 3. Declarative Alias Resolution
 */
class SubcircuitResolver {
public:
    /**
     * @brief Resolve a subcircuit by name.
     * @param name The name or path of the subcircuit (e.g., "Darlington Pair", "test_alu.sch").
     * @param contextDir Optional directory to search relative to (e.g., current schematic's dir).
     * @return ResolvedSubcircuit containing the resolution result.
     */
    static ResolvedSubcircuit resolve(const QString& name, const QString& contextDir = "");

    /**
     * @brief Get the standard library alias for a given name if it exists.
     * @return The canonical name in the Standard Cell Library, or empty string.
     */
    static QString resolveAlias(const QString& name);
};

} // namespace core
} // namespace gui
} // namespace acutesim
