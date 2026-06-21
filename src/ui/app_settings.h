#pragma once

#include "canvas.h"

enum class AppTheme { System, Light, Dark };

struct AppSettings {
    SystemType defaultSystemType = SystemType::Mechanical;
    bool snapToGrid = true;
    bool showGrid = true;
    int gridSpacing = 20;
    bool antialiasing = true;
    AppTheme theme = AppTheme::System;

    static AppSettings load();
    void save() const;
};

void applyAppTheme(AppTheme theme);
