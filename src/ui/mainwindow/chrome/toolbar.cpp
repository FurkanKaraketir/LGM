#include "mainwindow.h"
#include "detail.h"

using namespace mw;

#include "canvas.h"
#include "tool_icons.h"

#include <QActionGroup>
#include <QComboBox>
#include <QLabel>
#include <QStyle>
#include <QToolBar>
#include <QUndoStack>

void MainWindow::buildToolbar() {

    auto* toolbar = addToolBar(tr("Tools"));

    toolbar->setObjectName("mainToolBar");

    toolbar->setMovable(false);

    toolbar->setFloatable(false);

    toolbar->setIconSize({24, 24});

    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    // ponytail: no palette() in stylesheets — values freeze at parse time and break theme switches
    toolbar->setStyleSheet("QToolBar#mainToolBar { spacing: 4px; padding: 4px; }");



    m_modeGroup = new QActionGroup(this);

    m_modeGroup->setExclusive(true);



    m_selectAction =

        addTool(m_modeGroup, toolbar, ToolIcons::select(),

                tr("Select"), "tool.select", GraphScene::Mode::Select, m_view);

    m_addNodeAction = addTool(m_modeGroup, toolbar, ToolIcons::node(),

                               tr("Node"), "tool.addNode", GraphScene::Mode::AddNode, m_view);

    m_addBranchAction =

        addTool(m_modeGroup, toolbar, ToolIcons::branch(), tr("Branch"),

                "tool.addBranch", GraphScene::Mode::AddBranch, m_view);

    m_addTwoPortAction = addTool(m_modeGroup, toolbar, ToolIcons::twoPort(),

                                 tr("Two-Port"), "tool.addTwoPort", GraphScene::Mode::AddTwoPort,

                                 m_view);

    toolbar->addSeparator();

    auto* domainLabel = new QLabel(tr("Domain:"), this);

    domainLabel->setContentsMargins(8, 0, 4, 0);

    toolbar->addWidget(domainLabel);

    m_defaultSystemTypeCombo = new QComboBox(this);

    populateSystemTypeCombo(m_defaultSystemTypeCombo);

    m_defaultSystemTypeCombo->setToolTip(tr("Default system type for new nodes and branches"));

    connect(m_defaultSystemTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,

            [this](int index) {

                if (m_updatingDomainCombo) {

                    return;

                }

                m_scene->setDefaultSystemType(

                    static_cast<SystemType>(m_defaultSystemTypeCombo->itemData(index).toInt()));

            });

    toolbar->addWidget(m_defaultSystemTypeCombo);

    m_deleteAction = toolbar->addAction(themedIcon("edit-delete", QStyle::SP_TrashIcon), tr("Delete"));
    m_deleteAction->setObjectName(QStringLiteral("edit.delete"));
    m_deleteAction->setShortcutContext(Qt::ApplicationShortcut);
    m_deleteAction->setToolTip(tr("Delete selection"));
    connect(m_deleteAction, &QAction::triggered, m_scene, &GraphScene::pushDeleteSelection);

    m_selectAction->setChecked(true);



    toolbar->addSeparator();



    auto* homeAction =

        toolbar->addAction(themedIcon("go-home", QStyle::SP_DirHomeIcon), tr("Home"));

    homeAction->setObjectName(QStringLiteral("view.home"));

    homeAction->setShortcutContext(Qt::ApplicationShortcut);

    homeAction->setToolTip(tr("Center view on the graph"));

    connect(homeAction, &QAction::triggered, m_view, &GraphView::goHome);



    auto* zoomInAction =

        toolbar->addAction(themedIcon("zoom-in", QStyle::SP_ArrowUp), tr("Zoom In"));

    zoomInAction->setObjectName(QStringLiteral("view.zoomIn"));

    zoomInAction->setShortcutContext(Qt::ApplicationShortcut);

    zoomInAction->setToolTip(tr("Zoom in (Shift+scroll)"));

    connect(zoomInAction, &QAction::triggered, m_view, &GraphView::zoomIn);



    auto* zoomOutAction =

        toolbar->addAction(themedIcon("zoom-out", QStyle::SP_ArrowDown), tr("Zoom Out"));

    zoomOutAction->setObjectName(QStringLiteral("view.zoomOut"));

    zoomOutAction->setShortcutContext(Qt::ApplicationShortcut);

    zoomOutAction->setToolTip(tr("Zoom out (Shift+scroll)"));

    connect(zoomOutAction, &QAction::triggered, m_view, &GraphView::zoomOut);



    toolbar->addSeparator();



    toolbar->addAction(m_undoAction);

    toolbar->addAction(m_redoAction);



    toolbar->addSeparator();

    m_analyzeAction = toolbar->addAction(ToolIcons::analyze(), tr("Analyze"));
    m_analyzeAction->setObjectName(QStringLiteral("tool.analyze"));
    m_analyzeAction->setShortcutContext(Qt::ApplicationShortcut);
    m_analyzeAction->setToolTip(tr("Open analyze panel"));
    connect(m_analyzeAction, &QAction::triggered, this, &MainWindow::showAnalyzeWindow);

    m_consoleAction = toolbar->addAction(themedIcon("utilities-terminal", QStyle::SP_ComputerIcon),
                                         tr("Console"));
    m_consoleAction->setObjectName(QStringLiteral("tool.console"));
    m_consoleAction->setShortcutContext(Qt::ApplicationShortcut);
    m_consoleAction->setToolTip(tr("Show Qt log output"));
    connect(m_consoleAction, &QAction::triggered, this, &MainWindow::showConsoleWindow);

    connect(m_scene->undoStack(), &QUndoStack::canUndoChanged, m_undoAction, &QAction::setEnabled);

    connect(m_scene->undoStack(), &QUndoStack::canRedoChanged, m_redoAction, &QAction::setEnabled);

    connect(m_scene->undoStack(), &QUndoStack::undoTextChanged, this, [this](const QString& text) {

        m_undoAction->setToolTip(text.isEmpty() ? tr("Undo") : tr("Undo %1").arg(text));

    });

    connect(m_scene->undoStack(), &QUndoStack::redoTextChanged, this, [this](const QString& text) {

        m_redoAction->setToolTip(text.isEmpty() ? tr("Redo") : tr("Redo %1").arg(text));

    });

    connect(m_scene->undoStack(), &QUndoStack::indexChanged, this, [this]() {

        updateObjectList();

        updatePropertyPanel();

    });



    m_undoAction->setEnabled(false);

    m_redoAction->setEnabled(false);

}
