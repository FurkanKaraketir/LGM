#include "app_settings.h"

#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

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
