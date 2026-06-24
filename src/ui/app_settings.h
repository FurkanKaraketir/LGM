#pragma once

#include "canvas.h"

#include <QHash>
#include <QKeySequence>
#include <QString>

enum class AppTheme { System, Light, Dark };

struct AppSettings {
    SystemType defaultSystemType = SystemType::Mechanical;
    bool snapToGrid = true;
    bool showGrid = true;
    int gridSpacing = 20;
    bool antialiasing = true;
    AppTheme theme = AppTheme::System;
    QHash<QString, QString> shortcutOverrides;

    QKeySequence shortcut(const QString& id) const;
    static AppSettings load();
    void save() const;
};

void applyAppTheme(AppTheme theme);
