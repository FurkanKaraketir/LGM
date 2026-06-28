#include "mainwindow.h"
#include "detail.h"

using namespace mw;

#include "analyze_window.h"
#include "canvas.h"
#include "state_space.h"

#include <QAction>
#include <QDockWidget>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

void MainWindow::updateStateSpacePanel() {

    if (m_clearingDocument || !m_stateSpaceLayout || !m_stateSpaceScrollContent) {

        return;

    }

    while (QLayoutItem* child = m_stateSpaceLayout->takeAt(0)) {

        if (QWidget* widget = child->widget()) {

            delete widget;

        }

        delete child;

    }



    const lg::StateSpaceResult& stateSpace = m_scene->lastStateSpaceResult();

    lg::populateStateSpaceLayout(m_stateSpaceLayout, m_stateSpaceScrollContent, stateSpace);

    m_stateSpaceScrollContent->adjustSize();

    if (stateSpace.status == lg::StateSpaceResult::Status::Ok) {

        showOutputTab(kStateSpaceTab);

    } else {

        if (auto* panelAction = findChild<QAction*>(QStringLiteral("view.stateSpacePanel"))) {

            QSignalBlocker blocker(panelAction);

            panelAction->setChecked(false);

        }

        if (m_outputDock && m_outputTabs
            && m_outputTabs->currentIndex() == kStateSpaceTab) {
            m_outputDock->hide();
        }

    }

}
void MainWindow::showOutputTab(int index) {
    if (!m_outputDock || !m_outputTabs) {
        return;
    }
    m_outputTabs->setCurrentIndex(index);
    m_outputDock->show();
    m_outputDock->raise();
    syncStateSpacePanelAction();
}

void MainWindow::syncStateSpacePanelAction() {
    if (auto* panelAction = findChild<QAction*>(QStringLiteral("view.stateSpacePanel"))) {
        QSignalBlocker blocker(panelAction);
        const bool checked = m_outputDock && m_outputDock->isVisible() && m_outputTabs
                             && m_outputTabs->currentIndex() == kStateSpaceTab;
        panelAction->setChecked(checked);
    }
}
