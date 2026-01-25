#include "icon_provider.h"
#include "gui/styling/theme_manager.h"
#include <QFile>
#include <QPainter>
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
        return QIcon();
    }

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
    return QString(":/icons/icons/%1.svg").arg(name);
}

} // namespace core
} // namespace gui
} // namespace acutesim
