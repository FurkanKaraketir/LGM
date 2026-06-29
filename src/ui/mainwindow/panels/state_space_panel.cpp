#include "mainwindow.h"
#include "detail.h"

using namespace mw;

#include "analyze_window.h"
#include "canvas.h"
#include "state_space.h"

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

        showPanelTab(PanelTab::StateSpace);

    } else if (isPanelTabVisible(PanelTab::StateSpace)
               && m_outputTabs && m_outputTabs->currentIndex() >= 0
               && m_outputTabs->tabBar()->tabData(m_outputTabs->currentIndex()).toInt()
                      == static_cast<int>(PanelTab::StateSpace)) {
        hidePanelTab(PanelTab::StateSpace);
    }

}
