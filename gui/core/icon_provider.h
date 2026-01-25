#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QMap>
#include <QColor>

namespace acutesim {
namespace gui {
namespace core {

/**
 * IconProvider - Centralized SVG icon management
 *
 * Provides consistent, theme-aware icons across the application.
 * Icons are loaded from embedded Qt resources (:/icons/) and can be
 * recolored based on the active theme.
 *
 * Usage:
 *   QIcon folderIcon = IconProvider::icon("explorer");
 *   QIcon coloredIcon = IconProvider::icon("search", QColor("#007acc"));
 */
class IconProvider {
public:
    /**
     * Get an icon by name, using the default icon color from theme.
     * @param name Icon identifier (e.g., "explorer", "search")
     * @return QIcon loaded from SVG resource
     */
    static QIcon icon(const QString& name);

    /**
     * Get an icon with a specific color override.
     * @param name Icon identifier
     * @param color Color to apply to the SVG stroke/fill
     * @return QIcon with color applied
     */
    static QIcon icon(const QString& name, const QColor& color);

    /**
     * Get a pixmap of specified size.
     * @param name Icon identifier
     * @param size Desired pixel size (square)
     * @param color Optional color override
     * @return QPixmap rendered at specified size
     */
    static QPixmap pixmap(const QString& name, int size = 24, const QColor& color = QColor());

    /**
     * Clear the icon cache (call after theme change).
     */
    static void clearCache();

    /**
     * Register a custom icon path for a name.
     * @param name Icon identifier
     * @param resourcePath Qt resource path (e.g., ":/custom/myicon.svg")
     */
    static void registerIcon(const QString& name, const QString& resourcePath);

private:
    IconProvider() = default;

    static QIcon loadSvgIcon(const QString& path, const QColor& color);
    static QString getResourcePath(const QString& name);

    static QMap<QString, QString> customPaths_;
    static QMap<QString, QIcon> iconCache_;
};

} // namespace core
} // namespace gui
} // namespace acutesim
