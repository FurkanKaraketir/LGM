#include "mainwindow.h"
#include "detail.h"
#include "common_includes.h"

using namespace mw;

#include "app_settings.h"
#include "app_shortcuts.h"
#include "canvas.h"
#include "settings_window.h"
#include "tool_icons.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QMenuBar>
#include <QPainter>
#include <QShortcut>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QWidget>

void MainWindow::syncModeUi(GraphScene::Mode mode) {

    const bool manualTree = mode == GraphScene::Mode::SelectNormalTree;
    if (m_applyManualTreeReturnShortcut) {
        m_applyManualTreeReturnShortcut->setEnabled(manualTree);
    }
    if (m_applyManualTreeEnterShortcut) {
        m_applyManualTreeEnterShortcut->setEnabled(manualTree);
    }
    if (m_cancelManualTreeShortcut) {
        m_cancelManualTreeShortcut->setEnabled(manualTree);
    }

    switch (mode) {

    case GraphScene::Mode::Select:

        m_selectAction->setChecked(true);

        break;

    case GraphScene::Mode::AddNode:

        m_addNodeAction->setChecked(true);

        break;

    case GraphScene::Mode::AddBranch:

        m_addBranchAction->setChecked(true);

        break;

    case GraphScene::Mode::AddTwoPort:

        m_addTwoPortAction->setChecked(true);

        break;

    case GraphScene::Mode::SelectNormalTree:

        break;

    }

}
QString MainWindow::modeStatusText(GraphScene::Mode mode) {

    switch (mode) {

    case GraphScene::Mode::Select:

        return tr("Select — drag port nodes to reshape two-port; drag coupler to move whole element; "
                  "M combine nodes; F flip branch; T switch transformer/gyrator");

    case GraphScene::Mode::AddNode:

        return tr("Add node — click empty grid to place a node");

    case GraphScene::Mode::AddBranch:

        return tr("Add branch — click start node, then end node");

    case GraphScene::Mode::AddTwoPort:

        return tr("Add two-port — click empty grid; drag port nodes to reshape; combine ref nodes (M) for shared reference");

    case GraphScene::Mode::SelectNormalTree:

        return tr("Normal tree — click branches to toggle tree/link (green=tree); Enter to apply, Esc to cancel");

    }

    return {};

}
void MainWindow::syncDefaultSystemTypeCombo(SystemType type) {
    if (!m_defaultSystemTypeCombo) {
        return;
    }
    m_updatingDomainCombo = true;
    const int index = m_defaultSystemTypeCombo->findData(static_cast<int>(type));
    if (index >= 0) {
        m_defaultSystemTypeCombo->setCurrentIndex(index);
    }
    m_updatingDomainCombo = false;
}

AppSettings MainWindow::currentSettings() const {
    AppSettings s;
    s.theme = m_theme;
    s.defaultSystemType = m_scene->defaultSystemType();
    s.snapToGrid = m_scene->snapToGrid();
    s.showGrid = m_scene->showGrid();
    s.gridSpacing = static_cast<int>(m_scene->gridSpacing());
    s.antialiasing = m_view->renderHints().testFlag(QPainter::Antialiasing);

    for (QAction* action : findChildren<QAction*>()) {
        const QString& id = action->objectName();
        if (!AppShortcuts::isKnown(id)) {
            continue;
        }
        const QKeySequence current = action->shortcut();
        const QKeySequence def = AppShortcuts::defaultFor(id);
        if (current != def) {
            s.shortcutOverrides.insert(id, current.toString());
        }
    }
    return s;
}

void MainWindow::applyShortcuts(const AppSettings& settings) {
    for (QAction* action : findChildren<QAction*>()) {
        const QString& id = action->objectName();
        if (!AppShortcuts::isKnown(id)) {
            continue;
        }
        action->setShortcut(settings.shortcut(id));
        action->setShortcutContext(Qt::ApplicationShortcut);
        if (id == QLatin1String("tool.addNode") || id == QLatin1String("tool.addBranch")) {
            action->setToolTip(shortcutToolTip(action->text(), action->shortcut()));
        }
    }
}

void MainWindow::applySettings(const AppSettings& settings) {
    m_theme = settings.theme;
    applyAppTheme(settings.theme);
    m_scene->setDefaultSystemType(settings.defaultSystemType);
    syncDefaultSystemTypeCombo(settings.defaultSystemType);
    m_scene->setSnapToGrid(settings.snapToGrid);
    m_scene->setShowGrid(settings.showGrid);
    m_scene->setGridSpacing(settings.gridSpacing);
    m_view->setRenderHint(QPainter::Antialiasing, settings.antialiasing);
    applyShortcuts(settings);
    m_scene->refreshAppearance();
    refreshChromeTheme();
}

void MainWindow::refreshToolIcons() {
    if (m_selectAction) {
        m_selectAction->setIcon(ToolIcons::select());
    }
    if (m_addNodeAction) {
        m_addNodeAction->setIcon(ToolIcons::node());
    }
    if (m_addBranchAction) {
        m_addBranchAction->setIcon(ToolIcons::branch());
    }
    if (m_addTwoPortAction) {
        m_addTwoPortAction->setIcon(ToolIcons::twoPort());
    }
    if (m_analyzeAction) {
        m_analyzeAction->setIcon(ToolIcons::analyze());
    }
}

void MainWindow::refreshChromeTheme() {
    auto polishWidget = [](QWidget* widget) {
        if (!widget) {
            return;
        }
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    };

    polishWidget(menuBar());
    polishWidget(statusBar());
    if (auto* toolbar = findChild<QToolBar*>("mainToolBar")) {
        polishWidget(toolbar);
        for (QAction* action : toolbar->actions()) {
            polishWidget(toolbar->widgetForAction(action));
        }
    }
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        polishWidget(dock);
    }
    if (m_settingsWindow) {
        polishWidget(m_settingsWindow);
    }
    refreshToolIcons();
}

void MainWindow::showSettingsWindow() {
    if (!m_settingsWindow) {
        m_settingsWindow = new SettingsWindow;
        connect(m_settingsWindow, &SettingsWindow::settingsApplied, this, &MainWindow::applySettings);
    }
    m_settingsWindow->setFrom(currentSettings());
    m_settingsWindow->show();
    m_settingsWindow->raise();
    m_settingsWindow->activateWindow();
}

