#pragma once

#include "app_settings.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QPushButton;
class QSpinBox;

class SettingsWindow : public QWidget {
    Q_OBJECT

public:
    explicit SettingsWindow(QWidget* parent = nullptr);

    void setFrom(const AppSettings& settings);
    AppSettings settings() const;

signals:
    void settingsApplied(const AppSettings& settings);

private:
    void apply();

    QComboBox* m_defaultSystemType = nullptr;
    QComboBox* m_theme = nullptr;
    QCheckBox* m_snapToGrid = nullptr;
    QCheckBox* m_showGrid = nullptr;
    QSpinBox* m_gridSpacing = nullptr;
    QCheckBox* m_antialiasing = nullptr;
};
