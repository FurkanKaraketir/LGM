#include "app_settings.h"

#include "app_shortcuts.h"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

QKeySequence AppSettings::shortcut(const QString& id) const {
    const auto it = shortcutOverrides.constFind(id);
    if (it != shortcutOverrides.constEnd() && !it.value().isEmpty()) {
        return QKeySequence(*it);
    }
    return AppShortcuts::defaultFor(id);
}

AppSettings AppSettings::load() {
    QSettings settings;
    AppSettings s;
    s.defaultSystemType =
        static_cast<SystemType>(settings.value("defaultSystemType", static_cast<int>(SystemType::Mechanical))
                                    .toInt());
    s.snapToGrid = settings.value("snapToGrid", true).toBool();
    s.showGrid = settings.value("showGrid", true).toBool();
    s.gridSpacing = settings.value("gridSpacing", 20).toInt();
    s.antialiasing = settings.value("antialiasing", true).toBool();
    s.theme = static_cast<AppTheme>(settings.value("theme", static_cast<int>(AppTheme::System)).toInt());
    settings.beginGroup("shortcuts");
    for (const ShortcutDef& def : AppShortcuts::defs()) {
        const QString id = QString::fromLatin1(def.id);
        const QVariant value = settings.value(id);
        if (value.isValid()) {
            s.shortcutOverrides.insert(id, value.toString());
        }
    }
    settings.endGroup();
    return s;
}

void AppSettings::save() const {
    QSettings settings;
    settings.setValue("defaultSystemType", static_cast<int>(defaultSystemType));
    settings.setValue("snapToGrid", snapToGrid);
    settings.setValue("showGrid", showGrid);
    settings.setValue("gridSpacing", gridSpacing);
    settings.setValue("antialiasing", antialiasing);
    settings.setValue("theme", static_cast<int>(theme));
    settings.beginGroup("shortcuts");
    settings.remove("");
    for (auto it = shortcutOverrides.constBegin(); it != shortcutOverrides.constEnd(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
}

void applyAppTheme(AppTheme theme) {
    auto* hints = QGuiApplication::styleHints();
    switch (theme) {
    case AppTheme::System:
        hints->setColorScheme(Qt::ColorScheme::Unknown);
        break;
    case AppTheme::Light:
        hints->setColorScheme(Qt::ColorScheme::Light);
        break;
    case AppTheme::Dark:
        hints->setColorScheme(Qt::ColorScheme::Dark);
        break;
    }
}
