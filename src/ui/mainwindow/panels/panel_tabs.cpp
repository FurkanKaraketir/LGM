#include "mainwindow.h"
#include "detail.h"

using namespace mw;

#include <QAction>
#include <QDockWidget>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>

#include <cassert>

namespace {

int indexForPanelTab(QTabWidget* tabs, PanelTab tab) {
    if (!tabs) {
        return -1;
    }
    const int id = static_cast<int>(tab);
    for (int i = 0; i < tabs->count(); ++i) {
        if (tabs->tabBar()->tabData(i).toInt() == id) {
            return i;
        }
    }
    return -1;
}

bool anyVisibleTab(QTabWidget* tabs) {
    if (!tabs) {
        return false;
    }
    for (int i = 0; i < tabs->count(); ++i) {
        if (tabs->tabBar()->isTabVisible(i)) {
            return true;
        }
    }
    return false;
}

void switchFromClosedTab(QTabWidget* tabs, int closedIndex) {
    if (!tabs || tabs->currentIndex() != closedIndex) {
        return;
    }
    for (int i = 0; i < tabs->count(); ++i) {
        if (tabs->tabBar()->isTabVisible(i)) {
            tabs->setCurrentIndex(i);
            return;
        }
    }
}

}  // namespace

void MainWindow::showPanelTab(PanelTab tab) {
    QTabWidget* tabs = nullptr;
    QDockWidget* dock = nullptr;
    switch (tab) {
    case PanelTab::Properties:
    case PanelTab::Analyze:
        tabs = m_sidePanelTabs;
        dock = m_sidePanelDock;
        break;
    case PanelTab::Console:
    case PanelTab::StateSpace:
        tabs = m_outputTabs;
        dock = m_outputDock;
        break;
    }
    const int index = indexForPanelTab(tabs, tab);
    if (!tabs || !dock || index < 0) {
        return;
    }
    assert(index < tabs->count());
    tabs->tabBar()->setTabVisible(index, true);
    tabs->setCurrentIndex(index);
    dock->show();
    dock->raise();
    syncPanelMenuActions();
}

void MainWindow::hidePanelTab(PanelTab tab) {
    QTabWidget* tabs = nullptr;
    QDockWidget* dock = nullptr;
    switch (tab) {
    case PanelTab::Properties:
    case PanelTab::Analyze:
        tabs = m_sidePanelTabs;
        dock = m_sidePanelDock;
        break;
    case PanelTab::Console:
    case PanelTab::StateSpace:
        tabs = m_outputTabs;
        dock = m_outputDock;
        break;
    }
    const int index = indexForPanelTab(tabs, tab);
    if (!tabs || !dock || index < 0) {
        return;
    }
    tabs->tabBar()->setTabVisible(index, false);
    switchFromClosedTab(tabs, index);
    if (!anyVisibleTab(tabs)) {
        dock->hide();
    }
    syncPanelMenuActions();
}

bool MainWindow::isPanelTabVisible(PanelTab tab) const {
    QTabWidget* tabs = nullptr;
    switch (tab) {
    case PanelTab::Properties:
    case PanelTab::Analyze:
        tabs = m_sidePanelTabs;
        break;
    case PanelTab::Console:
    case PanelTab::StateSpace:
        tabs = m_outputTabs;
        break;
    }
    const int index = indexForPanelTab(tabs, tab);
    if (!tabs || index < 0) {
        return false;
    }
    return tabs->tabBar()->isTabVisible(index);
}

void MainWindow::syncPanelMenuActions() {
    const auto sync = [this](const char* name, PanelTab tab) {
        if (auto* action = findChild<QAction*>(name)) {
            QSignalBlocker blocker(action);
            action->setChecked(isPanelTabVisible(tab));
        }
    };
    sync("view.propertyPanel", PanelTab::Properties);
    sync("view.analyzePanel", PanelTab::Analyze);
    sync("view.consolePanel", PanelTab::Console);
    sync("view.stateSpacePanel", PanelTab::StateSpace);
}

void MainWindow::showAnalyzeWindow() {
    showPanelTab(PanelTab::Analyze);
}

void MainWindow::showPropertyPanel() {
    showPanelTab(PanelTab::Properties);
}

void MainWindow::raisePropertyPanelForSelection() {
    if (m_clearingDocument || m_blockSceneSelectionSync || !m_sidePanelTabs) {
        return;
    }
    if (m_scene->selectedItems().isEmpty()) {
        return;
    }
    const int analyzeIndex = indexForPanelTab(m_sidePanelTabs, PanelTab::Analyze);
    if (analyzeIndex < 0 || !m_sidePanelTabs->tabBar()->isTabVisible(analyzeIndex)
        || m_sidePanelTabs->currentIndex() != analyzeIndex) {
        return;
    }
    showPanelTab(PanelTab::Properties);
}

void MainWindow::showConsoleWindow() {
    showPanelTab(PanelTab::Console);
}
