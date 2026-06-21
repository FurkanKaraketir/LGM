#include "settings_window.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

void populateSystemTypeCombo(QComboBox* combo) {
    combo->addItem(QObject::tr("Mechanical (Translational)"),
                   static_cast<int>(SystemType::Mechanical));
    combo->addItem(QObject::tr("Mechanical (Rotational)"),
                   static_cast<int>(SystemType::MechanicalRotational));
    combo->addItem(QObject::tr("Electrical"), static_cast<int>(SystemType::Electrical));
    combo->addItem(QObject::tr("Fluid"), static_cast<int>(SystemType::Fluid));
    combo->addItem(QObject::tr("Heat"), static_cast<int>(SystemType::Heat));
}

}  // namespace

SettingsWindow::SettingsWindow(QWidget* parent) : QWidget(parent, Qt::Window) {
    setWindowTitle(tr("Settings"));
    setAttribute(Qt::WA_DeleteOnClose, false);

    m_defaultSystemType = new QComboBox(this);
    populateSystemTypeCombo(m_defaultSystemType);

    m_theme = new QComboBox(this);
    m_theme->addItem(tr("System"), static_cast<int>(AppTheme::System));
    m_theme->addItem(tr("Light"), static_cast<int>(AppTheme::Light));
    m_theme->addItem(tr("Dark"), static_cast<int>(AppTheme::Dark));

    m_snapToGrid = new QCheckBox(tr("Snap new items to grid"), this);
    m_showGrid = new QCheckBox(tr("Show grid"), this);

    m_gridSpacing = new QSpinBox(this);
    m_gridSpacing->setRange(5, 100);
    m_gridSpacing->setSingleStep(5);
    m_gridSpacing->setSuffix(tr(" px"));

    m_antialiasing = new QCheckBox(tr("Antialiased rendering"), this);

    auto* form = new QFormLayout;
    form->addRow(tr("Theme:"), m_theme);
    form->addRow(tr("Default domain:"), m_defaultSystemType);
    form->addRow(m_snapToGrid);
    form->addRow(m_showGrid);
    form->addRow(tr("Grid spacing:"), m_gridSpacing);
    form->addRow(m_antialiasing);

    auto* applyButton = new QPushButton(tr("Apply"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);
    connect(applyButton, &QPushButton::clicked, this, &SettingsWindow::apply);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(applyButton);
    buttons->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addLayout(buttons);
    resize(360, 250);
}

void SettingsWindow::setFrom(const AppSettings& settings) {
    const int themeIndex = m_theme->findData(static_cast<int>(settings.theme));
    if (themeIndex >= 0) {
        m_theme->setCurrentIndex(themeIndex);
    }
    const int typeIndex = m_defaultSystemType->findData(static_cast<int>(settings.defaultSystemType));
    if (typeIndex >= 0) {
        m_defaultSystemType->setCurrentIndex(typeIndex);
    }
    m_snapToGrid->setChecked(settings.snapToGrid);
    m_showGrid->setChecked(settings.showGrid);
    m_gridSpacing->setValue(settings.gridSpacing);
    m_antialiasing->setChecked(settings.antialiasing);
}

AppSettings SettingsWindow::settings() const {
    AppSettings s;
    s.theme = static_cast<AppTheme>(m_theme->currentData().toInt());
    s.defaultSystemType =
        static_cast<SystemType>(m_defaultSystemType->currentData().toInt());
    s.snapToGrid = m_snapToGrid->isChecked();
    s.showGrid = m_showGrid->isChecked();
    s.gridSpacing = m_gridSpacing->value();
    s.antialiasing = m_antialiasing->isChecked();
    return s;
}

void SettingsWindow::apply() {
    const AppSettings s = settings();
    s.save();
    emit settingsApplied(s);
}
