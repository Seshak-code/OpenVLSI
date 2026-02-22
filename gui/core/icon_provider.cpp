#include "icon_provider.h"
#include "gui/styling/theme_manager.h"
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QFont>
#include <QDebug>
#include <QRegularExpression>
#include <QImage>

#ifdef ACUTESIM_HAS_SVG
#include <QSvgRenderer>
#endif
#include <cstdio>

namespace acutesim {
namespace gui {
namespace core {

QMap<QString, QString> IconProvider::customPaths_;
QMap<QString, QIcon> IconProvider::iconCache_;

QIcon IconProvider::icon(const QString& name) {
    QColor color = styling::ThemeManager::instance().getColor("ui.text");
    return icon(name, color);
}

QIcon IconProvider::icon(const QString& name, const QColor& color) {
    QString cacheKey = name + "_" + color.name();
    if (iconCache_.contains(cacheKey)) {
        return iconCache_[cacheKey];
    }

    QString path = getResourcePath(name);
    QIcon loadedIcon = loadSvgIcon(path, color);

    if (!loadedIcon.isNull()) {
        iconCache_[cacheKey] = loadedIcon;
    }
    return loadedIcon;
}

QPixmap IconProvider::pixmap(const QString& name, int size, const QColor& color) {
    QColor useColor = color.isValid() ? color : styling::ThemeManager::instance().getColor("ui.text");
    QString path = getResourcePath(name);
    return loadSvgIcon(path, useColor).pixmap(size, size);
}

void IconProvider::clearCache() {
    iconCache_.clear();
}

void IconProvider::registerIcon(const QString& name, const QString& resourcePath) {
    customPaths_[name] = resourcePath;
    QStringList keysToRemove;
    for (auto it = iconCache_.begin(); it != iconCache_.end(); ++it) {
        if (it.key().startsWith(name + "_")) {
            keysToRemove.append(it.key());
        }
    }
    for (const QString& key : keysToRemove) {
        iconCache_.remove(key);
    }
}

QIcon IconProvider::loadSvgIcon(const QString& path, const QColor& color) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[IconProvider] FAILED to open icon:" << path;
        return QIcon();
    }

#ifdef __EMSCRIPTEN__
    // SVG rasterization via FreeType causes unreachable traps in WASM.
    // Generate a simple letter-based colored icon as a visual fallback.
    file.close();
    QString baseName = QFileInfo(path).baseName(); // e.g. "explorer" from ":/icons/icons/explorer.svg"
    QString label = baseName.left(1).toUpper();

    QIcon fallbackIcon;
    for (int sz : {16, 24, 28, 32, 48}) {
        QPixmap pix(sz, sz);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        // Filled rounded rect in the icon's tint color
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(1, 1, sz - 2, sz - 2), 3, 3);
        // Single letter label in dark ink
        p.setPen(QColor(15, 15, 15));
        QFont f;
        f.setPixelSize(qMax(8, sz / 2));
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 0, sz, sz), Qt::AlignCenter, label);
        p.end();
        fallbackIcon.addPixmap(pix);
    }
    return fallbackIcon;
#endif

    qDebug() << "[IconProvider] Loaded icon:" << path << "color:" << color.name();

    QString svgContent = file.readAll();
    file.close();

    // Replace stroke/fill colors in SVG content
    svgContent.replace(QRegularExpression("stroke=\"[^\"]*\""),
                       QString("stroke=\"%1\"").arg(color.name()));
    svgContent.replace(QRegularExpression("fill=\"(?!none)[^\"]*\""),
                       QString("fill=\"%1\"").arg(color.name()));
    svgContent.replace("currentColor", color.name());

    QIcon icon;

#ifdef ACUTESIM_HAS_SVG
    for (int size : {16, 24, 28, 32, 48}) {
        QSvgRenderer renderer(svgContent.toUtf8());
        if (!renderer.isValid()) continue;
        
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        painter.end();

        icon.addPixmap(pixmap);
    }
#else
    QImage img;
    img.loadFromData(svgContent.toUtf8(), "SVG");
    if (img.isNull()) {
        img.load(path);
    }

    if (!img.isNull()) {
        for (int size : {16, 24, 28, 32, 48}) {
            QImage scaled = img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            icon.addPixmap(QPixmap::fromImage(scaled));
        }
    }
#endif

    return icon;
}

QString IconProvider::getResourcePath(const QString& name) {
    if (customPaths_.contains(name)) return customPaths_[name];
    QString path = QString(":/icons/icons/%1.svg").arg(name);

    // Debug: Check if resource exists
    if (!QFile::exists(path)) {
        qWarning() << "[IconProvider] Resource not found:" << path;
    }

    return path;
}

} // namespace core
} // namespace gui
} // namespace acutesim
