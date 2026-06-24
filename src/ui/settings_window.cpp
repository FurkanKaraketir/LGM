#include "settings_window.h"

#include "app_shortcuts.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
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

    auto* generalForm = new QFormLayout;
    generalForm->addRow(tr("Theme:"), m_theme);
    generalForm->addRow(tr("Default domain:"), m_defaultSystemType);
    generalForm->addRow(m_snapToGrid);
    generalForm->addRow(m_showGrid);
    generalForm->addRow(tr("Grid spacing:"), m_gridSpacing);
    generalForm->addRow(m_antialiasing);

    auto* resetGeneralButton = new QPushButton(tr("Reset General to Defaults"), this);
    connect(resetGeneralButton, &QPushButton::clicked, this, &SettingsWindow::resetGeneral);

    auto* generalLayout = new QVBoxLayout;
    generalLayout->addLayout(generalForm);
    generalLayout->addWidget(resetGeneralButton);

    auto* generalPage = new QWidget(this);
    generalPage->setLayout(generalLayout);

    m_shortcutTable = new QTableWidget(this);
    m_shortcutTable->setColumnCount(2);
    m_shortcutTable->setHorizontalHeaderLabels({tr("Action"), tr("Shortcut")});
    m_shortcutTable->horizontalHeader()->setStretchLastSection(true);
    m_shortcutTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_shortcutTable->verticalHeader()->setVisible(false);
    m_shortcutTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_shortcutTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* resetShortcutsButton = new QPushButton(tr("Reset Shortcuts to Defaults"), this);
    connect(resetShortcutsButton, &QPushButton::clicked, this, &SettingsWindow::resetShortcuts);

    auto* shortcutsLayout = new QVBoxLayout;
    shortcutsLayout->addWidget(m_shortcutTable);
    shortcutsLayout->addWidget(resetShortcutsButton);

    auto* shortcutsPage = new QWidget(this);
    shortcutsPage->setLayout(shortcutsLayout);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(generalPage, tr("General"));
    tabs->addTab(shortcutsPage, tr("Shortcuts"));

    auto* applyButton = new QPushButton(tr("Apply"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);
    connect(applyButton, &QPushButton::clicked, this, &SettingsWindow::apply);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(applyButton);
    buttons->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addLayout(buttons);
    resize(480, 420);
}

void SettingsWindow::populateShortcutTable(const AppSettings& settings) {
    m_shortcutTable->setRowCount(0);
    for (const ShortcutDef& def : AppShortcuts::defs()) {
        const int row = m_shortcutTable->rowCount();
        m_shortcutTable->insertRow(row);

        auto* labelItem = new QTableWidgetItem(tr(def.label));
        labelItem->setData(Qt::UserRole, QString::fromLatin1(def.id));
        m_shortcutTable->setItem(row, 0, labelItem);

        auto* editor = new QKeySequenceEdit(m_shortcutTable);
        editor->setKeySequence(settings.shortcut(QString::fromLatin1(def.id)));
        m_shortcutTable->setCellWidget(row, 1, editor);
    }
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
    populateShortcutTable(settings);
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

    for (int row = 0; row < m_shortcutTable->rowCount(); ++row) {
        const QString id = m_shortcutTable->item(row, 0)->data(Qt::UserRole).toString();
        const auto* editor = qobject_cast<const QKeySequenceEdit*>(m_shortcutTable->cellWidget(row, 1));
        if (!editor) {
            continue;
        }
        const QKeySequence sequence = editor->keySequence();
        if (sequence == AppShortcuts::defaultFor(id)) {
            s.shortcutOverrides.remove(id);
        } else {
            s.shortcutOverrides.insert(id, sequence.toString());
        }
    }
    return s;
}

void SettingsWindow::resetGeneral() {
    AppSettings current = settings();
    AppSettings defaults;
    defaults.shortcutOverrides = current.shortcutOverrides;
    setFrom(defaults);
}

void SettingsWindow::resetShortcuts() {
    for (int row = 0; row < m_shortcutTable->rowCount(); ++row) {
        const QString id = m_shortcutTable->item(row, 0)->data(Qt::UserRole).toString();
        if (auto* editor = qobject_cast<QKeySequenceEdit*>(m_shortcutTable->cellWidget(row, 1))) {
            editor->setKeySequence(AppShortcuts::defaultFor(id));
        }
    }
}

void SettingsWindow::apply() {
    const AppSettings s = settings();
    s.save();
    emit settingsApplied(s);
}
