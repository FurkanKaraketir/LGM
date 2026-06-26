#include "mainwindow.h"
#include "detail.h"
#include "common_includes.h"

using namespace mw;

#include "analyze_window.h"
#include "app_log.h"
#include "app_update.h"
#include "canvas.h"
#include "elemental_equation.h"
#include "guide_window.h"
#include "normal_tree.h"
#include "state_space.h"
#include "tool_icons.h"

#include <QActionGroup>
#include <QApplication>
#include <QDate>
#include <QDir>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStyle>
#include <QToolBar>

void MainWindow::buildMenuBar() {

    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    

    auto* newAction = fileMenu->addAction(themedIcon("document-new", QStyle::SP_FileIcon), tr("&New"));

    newAction->setObjectName(QStringLiteral("file.new"));

    connect(newAction, &QAction::triggered, this, &MainWindow::fileNew);

    

    auto* openAction = fileMenu->addAction(themedIcon("document-open", QStyle::SP_DirIcon), tr("&Open..."));

    openAction->setObjectName(QStringLiteral("file.open"));

    connect(openAction, &QAction::triggered, this, &MainWindow::fileOpen);

    auto* examplesMenu = fileMenu->addMenu(tr("E&xamples"));
    const QDir exampleDir(examplesDir());
    for (const QString& fileName :
         exampleDir.entryList({QStringLiteral("*.lgm")}, QDir::Files, QDir::Name)) {
        const QString path = exampleDir.absoluteFilePath(fileName);
        auto* exampleAction = examplesMenu->addAction(fileName);
        connect(exampleAction, &QAction::triggered, this,
                [this, path]() { fileOpenExample(path); });
    }

    auto* saveAction = fileMenu->addAction(themedIcon("document-save", QStyle::SP_DialogSaveButton), tr("&Save"));

    saveAction->setObjectName(QStringLiteral("file.save"));

    connect(saveAction, &QAction::triggered, this, [this](bool) { fileSave(); });

    

    auto* saveAsAction = fileMenu->addAction(themedIcon("document-save-as", QStyle::SP_DialogSaveButton), tr("Save &As..."));

    saveAsAction->setObjectName(QStringLiteral("file.saveAs"));

    connect(saveAsAction, &QAction::triggered, this, &MainWindow::fileSaveAs);

    

    fileMenu->addSeparator();

    

    auto* exitAction = fileMenu->addAction(themedIcon("application-exit", QStyle::SP_DialogCloseButton), tr("E&xit"));

    exitAction->setObjectName(QStringLiteral("file.quit"));

    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);



    auto* editMenu = menuBar()->addMenu(tr("&Edit"));

    editMenu->addAction(m_undoAction);

    editMenu->addAction(m_redoAction);

    editMenu->addSeparator();

    auto* settingsAction = editMenu->addAction(tr("&Settings..."));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettingsWindow);

    editMenu->addSeparator();

    m_flipBranchAction = editMenu->addAction(tr("Flip &Branch"));

    m_flipBranchAction->setObjectName(QStringLiteral("edit.flipBranch"));

    m_flipBranchAction->setShortcutContext(Qt::ApplicationShortcut);

    connect(m_flipBranchAction, &QAction::triggered, this, [this]() {

        BranchItem* branch = singleSelectedBranch(m_scene);

        if (!branch && m_propertyTargetPtr &&

            m_propertyTargetKind == static_cast<int>(GraphObjectKind::Branch)) {

            branch = static_cast<BranchItem*>(m_propertyTargetPtr);

        }

        if (branch) {

            m_scene->pushFlipBranch(branch);

            updatePropertyPanel();

        }

    });

    m_mergeNodesAction = editMenu->addAction(tr("Combine &Nodes"));

    m_mergeNodesAction->setObjectName(QStringLiteral("edit.mergeNodes"));

    m_mergeNodesAction->setShortcutContext(Qt::ApplicationShortcut);

    connect(m_mergeNodesAction, &QAction::triggered, this, [this]() {

        QList<NodeItem*> nodes;

        for (QGraphicsItem* item : m_scene->selectedItems()) {

            if (auto* node = dynamic_cast<NodeItem*>(item)) {

                if (!nodes.contains(node)) {

                    nodes.push_back(node);

                }

            }

        }

        if (nodes.size() == 2) {

            m_scene->pushMergeNodes(nodes[0], nodes[1]);

        }

    });



    auto* selectAllAction = editMenu->addAction(tr("Select &All"));

    selectAllAction->setObjectName(QStringLiteral("edit.selectAll"));

    connect(selectAllAction, &QAction::triggered, this, [this]() {

        m_blockSceneSelectionSync = true;

        m_scene->clearSelection();

        m_objectTree->clearSelection();

        m_blockSceneSelectionSync = false;

        for (QGraphicsItem* item : m_scene->items()) {

            if (dynamic_cast<TwoPortItem*>(item)) {

                static_cast<TwoPortItem*>(item)->setSelected(true);

            }

        }

        for (QGraphicsItem* item : m_scene->items()) {

            if (auto* node = dynamic_cast<NodeItem*>(item)) {

                if (!node->twoPort()) {

                    node->setSelected(true);

                }

            } else if (auto* branch = dynamic_cast<BranchItem*>(item)) {

                if (!m_scene->twoPortFor(branch)) {

                    branch->setSelected(true);

                }

            }

        }

    });



    auto* clearSelectionAction = editMenu->addAction(tr("Clear Selection"));

    clearSelectionAction->setObjectName(QStringLiteral("edit.clearSelection"));

    connect(clearSelectionAction, &QAction::triggered, this, [this]() {

        m_blockSceneSelectionSync = true;

        m_scene->clearSelection();

        m_objectTree->clearSelection();

        m_blockSceneSelectionSync = false;

        updatePropertyPanel();

    });



    auto* viewMenu = menuBar()->addMenu(tr("&View"));

    

    auto* toolbarAction = viewMenu->addAction(tr("&Toolbar"));

    toolbarAction->setCheckable(true);

    toolbarAction->setChecked(true);

    connect(toolbarAction, &QAction::toggled, this, [this](bool checked) {

        if (auto* tb = findChild<QToolBar*>("mainToolBar")) {

            tb->setVisible(checked);

        }

    });

    

    viewMenu->addSeparator();

    

    auto* propertyPanelAction = viewMenu->addAction(tr("&Properties Panel"));

    propertyPanelAction->setCheckable(true);

    propertyPanelAction->setChecked(true);

    connect(propertyPanelAction, &QAction::toggled, this, [this](bool checked) {

        if (m_propertyDock) {

            m_propertyDock->setVisible(checked);

        }

    });

    

    auto* objectListAction = viewMenu->addAction(tr("&Objects"));

    objectListAction->setCheckable(true);

    objectListAction->setChecked(true);

    connect(objectListAction, &QAction::toggled, this, [this](bool checked) {

        if (m_objectListDock) {

            m_objectListDock->setVisible(checked);

        }

    });

    auto* analyzePanelAction = viewMenu->addAction(tr("&Analyze Panel"));
    analyzePanelAction->setObjectName(QStringLiteral("view.analyzePanel"));
    analyzePanelAction->setCheckable(true);
    analyzePanelAction->setChecked(false);
    connect(analyzePanelAction, &QAction::toggled, this, [this](bool checked) {
        if (m_analyzeDock) {
            m_analyzeDock->setVisible(checked);
        }
    });

    

    auto* stateSpacePanelAction = viewMenu->addAction(tr("State &Space Results"));

    stateSpacePanelAction->setObjectName(QStringLiteral("view.stateSpacePanel"));

    stateSpacePanelAction->setCheckable(true);

    stateSpacePanelAction->setChecked(false);

    connect(stateSpacePanelAction, &QAction::toggled, this, [this](bool checked) {
        if (!m_outputDock || !m_outputTabs) {
            return;
        }
        if (checked) {
            showOutputTab(kStateSpaceTab);
        } else if (m_outputTabs->currentIndex() == kStateSpaceTab) {
            m_outputDock->hide();
        }
    });

    

    viewMenu->addSeparator();

    

    auto* resetLayoutAction = viewMenu->addAction(tr("&Reset Layout"));

    connect(resetLayoutAction, &QAction::triggered, this, [this]() {

        removeDockWidget(m_propertyDock);

        removeDockWidget(m_objectListDock);

        removeDockWidget(m_analyzeDock);

        addDockWidget(Qt::LeftDockWidgetArea, m_objectListDock);

        addDockWidget(Qt::RightDockWidgetArea, m_propertyDock);

        addDockWidget(Qt::RightDockWidgetArea, m_analyzeDock);

        tabifyDockWidget(m_propertyDock, m_analyzeDock);

        removeDockWidget(m_outputDock);
        addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);

        m_objectListDock->show();

        m_propertyDock->show();

    });



    auto* analysisMenu = menuBar()->addMenu(tr("&Analysis"));

    auto* startAnalyzingAction = analysisMenu->addAction(tr("Start &Analyzing..."));
    startAnalyzingAction->setObjectName(QStringLiteral("analysis.start"));
    connect(startAnalyzingAction, &QAction::triggered, this, &MainWindow::showAnalyzeWindow);

    analysisMenu->addSeparator();

    auto* findNormalTreeAction =
        analysisMenu->addAction(tr("Find All &Normal Trees..."));

    findNormalTreeAction->setObjectName(QStringLiteral("analysis.normalTree"));

    connect(findNormalTreeAction, &QAction::triggered, this, [this]() {

        const lg::NormalTreeEnumerationResult all = m_scene->findAllNormalTrees();

        if (!all.trees.empty()) {

            const lg::NormalTreeResult& normalTree = m_scene->lastNormalTreeResult();

            int branchCount = 0;

            for (QGraphicsItem* item : m_scene->items()) {

                if (dynamic_cast<BranchItem*>(item)) {

                    ++branchCount;

                }

            }

            QStringList stateVarText;
            stateVarText.reserve(static_cast<int>(normalTree.stateVariables.size()));
            for (const lg::NormalTreeResult::StateVariable& state : normalTree.stateVariables) {
                stateVarText.push_back(state.symbol);
            }

            const QString stateVarSummary =
                stateVarText.isEmpty() ? tr("none") : stateVarText.join(QStringLiteral(", "));

            QString statusMessage =
                tr("Found %1 valid normal trees. Showing tree 1 — %2 branches, %3 links, order %4: %5.")
                    .arg(all.trees.size())
                    .arg(normalTree.treeBranches.size())
                    .arg(branchCount - static_cast<int>(normalTree.treeBranches.size()))
                    .arg(normalTree.stateVariables.size())
                    .arg(stateVarSummary);
            if (!all.message.isEmpty()) {
                statusMessage += QStringLiteral(" ") + all.message;
            }

            statusBar()->showMessage(statusMessage, 10000);

            updatePropertyPanel();

            return;

        }

        QMessageBox::warning(this, tr("Normal Tree"),

                             all.message.isEmpty()

                                 ? tr("Could not find a valid normal tree.")

                                 : all.message);

        statusBar()->showMessage(tr("Normal tree search failed."), 3000);

    });

    auto* stateSpaceAction = analysisMenu->addAction(tr("Compute &State Space..."));

    stateSpaceAction->setObjectName(QStringLiteral("analysis.stateSpace"));

    connect(stateSpaceAction, &QAction::triggered, this, [this]() {

        const lg::StateSpaceResult result = m_scene->computeStateSpaceRep();

        if (result.status == lg::StateSpaceResult::Status::Ok) {

            statusBar()->showMessage(result.message, 10000);

            updateStateSpacePanel();

            return;

        }

        QMessageBox::warning(this, tr("State Space"),

                             result.message.isEmpty() ? tr("State-space computation failed.")

                                                      : result.message);

        statusBar()->showMessage(tr("State-space computation failed."), 3000);

    });

    auto* clearNormalTreeAction = analysisMenu->addAction(tr("&Clear Normal Tree Highlight"));

    connect(clearNormalTreeAction, &QAction::triggered, this, [this]() {

        m_scene->clearNormalTreeHighlight();

        statusBar()->showMessage(tr("Normal tree highlight cleared."), 2000);

    });

    auto* selectNormalTreeAction =
        analysisMenu->addAction(tr("Select Normal Tree..."));

    selectNormalTreeAction->setObjectName(QStringLiteral("analysis.selectNormalTree"));

    connect(selectNormalTreeAction, &QAction::triggered, this, [this]() {

        m_scene->beginManualNormalTreeSelection();

        m_view->setToolMode(GraphScene::Mode::SelectNormalTree);

        updatePropertyPanel();

    });



    auto* helpMenu = menuBar()->addMenu(tr("&Help"));

    auto* guidesMenu = helpMenu->addMenu(tr("&Guides"));

    auto* quickStartGuide = guidesMenu->addAction(tr("&Quick Start"));
    connect(quickStartGuide, &QAction::triggered, this, [this]() {
        GuideWindow::showGuide(tr("Quick Start"), QStringLiteral(":/guides/quick_start.md"), this);
    });

    auto* stateSpaceGuide = guidesMenu->addAction(tr("&State-Space Derivation"));
    connect(stateSpaceGuide, &QAction::triggered, this, [this]() {
        GuideWindow::showGuide(tr("State-Space Derivation"),
                               QStringLiteral(":/guides/state_space_from_normal_tree.md"),
                               this);
    });

    auto* normalTreeGuide = guidesMenu->addAction(tr("Multiple &Normal Tree Finding"));
    connect(normalTreeGuide, &QAction::triggered, this, [this]() {
        GuideWindow::showGuide(tr("Multiple Normal Tree Finding"),
                               QStringLiteral(":/guides/multiple_normal_tree_finding.md"),
                               this);
    });

    helpMenu->addSeparator();

    auto* checkUpdatesAction =
        helpMenu->addAction(tr("Check for &Updates..."));
    connect(checkUpdatesAction, &QAction::triggered, this, [this]() {
        checkForUpdates(this, false);
    });

    auto* aboutAction = helpMenu->addAction(themedIcon("help-about", QStyle::SP_MessageBoxInformation), tr("&About"));

    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this, tr("About LGM"),
            tr("<h3>LGM</h3>"
               "<p>%1</p>"
               "<p>%2</p>"
               "<p>%3</p>"
               "<p>%4</p>"
               "<p>Copyright &copy; %5 Furkan Karaketir</p>"
               "<p><a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">%6</a></p>")
                .arg(tr("Linear graph modeling and state-space analysis."),
                     tr("Version %1").arg(QApplication::applicationVersion()),
                     tr("This program is free software; you can redistribute it and/or modify "
                        "it under the terms of the GNU General Public License as published by "
                        "the Free Software Foundation, either version 3 of the License, or "
                        "(at your option) any later version."),
                     tr("Based on Altun, Kerem; Balkan, R. Tuna; &amp; Platin, Bülent (2002). "
                        "<i>Extraction of state variable representations of dynamic systems "
                        "employing linear graph theory.</i> "
                        "The Sixth International Conference on Mechatronic Design and Modeling, "
                        "112&ndash;121. "
                        "<a href=\"https://doi.org/10.13140/RG.2.2.29940.96647\">"
                        "doi:10.13140/RG.2.2.29940.96647</a>."),
                     QString::number(QDate::currentDate().year()),
                     tr("GNU General Public License v3.0")));
    });

}
