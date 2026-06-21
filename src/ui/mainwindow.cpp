#include "mainwindow.h"



#include "canvas.h"
#include "settings_window.h"

#include "elemental_equation.h"
#include "latex_widget.h"
#include "state_space.h"



#include <QActionGroup>

#include <QApplication>

#include <QCloseEvent>

#include <QComboBox>

#include <QDockWidget>

#include <QFile>

#include <QFileDialog>

#include <QFileInfo>

#include <QGuiApplication>

#include <QHeaderView>

#include <QLabel>

#include <QLineEdit>

#include <QListWidget>

#include <QTreeWidget>

#include <QTreeWidgetItemIterator>

#include <QMenuBar>

#include <QMessageBox>

#include <QSignalBlocker>

#include <QPushButton>

#include <QStatusBar>

#include <QStyle>

#include <QStyleHints>

#include <QTableWidget>

#include <QToolBar>

#include <QVBoxLayout>



namespace {



enum class GraphObjectKind { Node, Branch, TwoPort };

constexpr int kObjectPtrRole = Qt::UserRole;

constexpr int kObjectKindRole = Qt::UserRole + 1;



void setItemObject(QTreeWidgetItem* item, GraphObjectKind kind, void* ptr) {

    item->setData(0, kObjectPtrRole, reinterpret_cast<quintptr>(ptr));

    item->setData(0, kObjectKindRole, static_cast<int>(kind));

}



void* objectPtr(const QTreeWidgetItem* item) {

    return reinterpret_cast<void*>(item->data(0, kObjectPtrRole).value<quintptr>());

}



GraphObjectKind objectKind(const QTreeWidgetItem* item) {

    return static_cast<GraphObjectKind>(item->data(0, kObjectKindRole).toInt());

}

void populateSystemTypeCombo(QComboBox* combo) {
    combo->addItem(QObject::tr("Mechanical (Translational)"),
                   static_cast<int>(SystemType::Mechanical));
    combo->addItem(QObject::tr("Mechanical (Rotational)"),
                   static_cast<int>(SystemType::MechanicalRotational));
    combo->addItem(QObject::tr("Electrical"), static_cast<int>(SystemType::Electrical));
    combo->addItem(QObject::tr("Fluid"), static_cast<int>(SystemType::Fluid));
    combo->addItem(QObject::tr("Heat"), static_cast<int>(SystemType::Heat));
}



BranchItem* singleSelectedBranch(const GraphScene* scene) {

    BranchItem* branch = nullptr;

    for (QGraphicsItem* item : scene->selectedItems()) {

        if (auto* selectedBranch = dynamic_cast<BranchItem*>(item)) {

            if (branch) {

                return nullptr;

            }

            branch = selectedBranch;

        }

    }

    return branch;

}



QIcon themedIcon(const char* name, QStyle::StandardPixmap fallback) {

    QIcon icon = QIcon::fromTheme(name);

    if (icon.isNull()) {

        icon = QApplication::style()->standardIcon(fallback);

    }

    return icon;

}



QAction* addTool(QActionGroup* group, QToolBar* toolbar, const QIcon& icon, const QString& text,

                 const QKeySequence& shortcut, GraphScene::Mode mode, GraphView* view) {

    auto* action = toolbar->addAction(icon, text);

    action->setCheckable(true);

    action->setShortcut(shortcut);

    action->setShortcutContext(Qt::ApplicationShortcut);

    action->setData(static_cast<int>(mode));

    group->addAction(action);

    QObject::connect(action, &QAction::triggered, view, [view, mode] { view->setToolMode(mode); });

    return action;

}



}  // namespace



MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {

    m_scene = new GraphScene(this);

    m_view = new GraphView(m_scene, this);



    m_view->setRenderHint(QPainter::Antialiasing);

    setCentralWidget(m_view);

    

    m_undoAction = new QAction(themedIcon("edit-undo", QStyle::SP_ArrowBack), tr("Undo"), this);

    m_undoAction->setShortcut(QKeySequence::Undo);

    m_undoAction->setShortcutContext(Qt::ApplicationShortcut);

    m_undoAction->setEnabled(false);

    connect(m_undoAction, &QAction::triggered, m_scene, &GraphScene::undo);



    m_redoAction = new QAction(themedIcon("edit-redo", QStyle::SP_ArrowForward), tr("Redo"), this);

    m_redoAction->setShortcut(QKeySequence::Redo);

    m_redoAction->setShortcutContext(Qt::ApplicationShortcut);

    m_redoAction->setEnabled(false);

    connect(m_redoAction, &QAction::triggered, m_scene, &GraphScene::redo);



    buildMenuBar();

    buildToolbar();

    buildStatusBar();

    buildDockPanels();

    applySettings(AppSettings::load());

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        refreshChromeTheme();
        m_scene->refreshAppearance();
    });

    connect(m_scene, &GraphScene::modeChanged, this, &MainWindow::syncModeUi);

    connect(m_scene->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateWindowTitle);

    connect(m_scene->undoStack(), &QUndoStack::indexChanged, this, &MainWindow::updateWindowTitle);



    updateWindowTitle();

    resize(1200, 800);

    

    setDockNestingEnabled(true);

}



void MainWindow::buildMenuBar() {

    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    

    auto* newAction = fileMenu->addAction(themedIcon("document-new", QStyle::SP_FileIcon), tr("&New"));

    newAction->setShortcut(QKeySequence::New);

    connect(newAction, &QAction::triggered, this, &MainWindow::fileNew);

    

    auto* openAction = fileMenu->addAction(themedIcon("document-open", QStyle::SP_DirIcon), tr("&Open..."));

    openAction->setShortcut(QKeySequence::Open);

    connect(openAction, &QAction::triggered, this, &MainWindow::fileOpen);

    

    auto* saveAction = fileMenu->addAction(themedIcon("document-save", QStyle::SP_DialogSaveButton), tr("&Save"));

    saveAction->setShortcut(QKeySequence::Save);

    connect(saveAction, &QAction::triggered, this, &MainWindow::fileSave);

    

    auto* saveAsAction = fileMenu->addAction(themedIcon("document-save-as", QStyle::SP_DialogSaveButton), tr("Save &As..."));

    saveAsAction->setShortcut(QKeySequence::SaveAs);

    connect(saveAsAction, &QAction::triggered, this, &MainWindow::fileSaveAs);

    

    fileMenu->addSeparator();

    

    auto* exitAction = fileMenu->addAction(themedIcon("application-exit", QStyle::SP_DialogCloseButton), tr("E&xit"));

    exitAction->setShortcut(QKeySequence::Quit);

    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);



    auto* editMenu = menuBar()->addMenu(tr("&Edit"));

    editMenu->addAction(m_undoAction);

    editMenu->addAction(m_redoAction);

    editMenu->addSeparator();

    auto* settingsAction = editMenu->addAction(tr("&Settings..."));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettingsWindow);

    editMenu->addSeparator();

    m_flipBranchAction = editMenu->addAction(tr("Flip &Branch"));

    m_flipBranchAction->setShortcut(QKeySequence(Qt::Key_F));

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

    m_mergeNodesAction->setShortcut(QKeySequence(Qt::Key_M));

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

    selectAllAction->setShortcut(QKeySequence::SelectAll);



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

    connect(propertyPanelAction, &QAction::toggled, m_propertyDock, &QDockWidget::setVisible);

    

    auto* objectListAction = viewMenu->addAction(tr("&Object List"));

    objectListAction->setCheckable(true);

    objectListAction->setChecked(true);

    connect(objectListAction, &QAction::toggled, m_objectListDock, &QDockWidget::setVisible);

    

    viewMenu->addSeparator();

    

    auto* resetLayoutAction = viewMenu->addAction(tr("&Reset Layout"));

    connect(resetLayoutAction, &QAction::triggered, this, [this]() {

        removeDockWidget(m_propertyDock);

        removeDockWidget(m_objectListDock);

        addDockWidget(Qt::LeftDockWidgetArea, m_objectListDock);

        addDockWidget(Qt::RightDockWidgetArea, m_propertyDock);

        m_objectListDock->show();

        m_propertyDock->show();

    });



    auto* analysisMenu = menuBar()->addMenu(tr("&Analysis"));

    auto* findNormalTreeAction =
        analysisMenu->addAction(tr("Find &Normal Tree..."));

    findNormalTreeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));

    connect(findNormalTreeAction, &QAction::triggered, this, [this]() {

        const lg::NormalTreeResult result = m_scene->findNormalTree();

        if (result.status == lg::NormalTreeResult::Status::Ok) {

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

            statusBar()->showMessage(

                tr("Normal tree — %1 branches, %2 links, order %3: %4")
                    .arg(normalTree.treeBranches.size())
                    .arg(branchCount - static_cast<int>(normalTree.treeBranches.size()))
                    .arg(normalTree.stateVariables.size())
                    .arg(stateVarSummary),

                10000);

            updatePropertyPanel();

            return;

        }

        QMessageBox::warning(this, tr("Normal Tree"),

                             result.message.isEmpty()

                                 ? tr("Could not find a valid normal tree.")

                                 : result.message);

        statusBar()->showMessage(tr("Normal tree search failed."), 3000);

    });

    auto* stateSpaceAction = analysisMenu->addAction(tr("Compute &State Space..."));

    stateSpaceAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));

    connect(stateSpaceAction, &QAction::triggered, this, [this]() {

        const lg::StateSpaceResult result = m_scene->computeStateSpaceRep();

        if (result.status == lg::StateSpaceResult::Status::Ok) {

            statusBar()->showMessage(result.message, 10000);

            updatePropertyPanel();

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

        updatePropertyPanel();

        statusBar()->showMessage(tr("Normal tree highlight cleared."), 2000);

    });



    auto* helpMenu = menuBar()->addMenu(tr("&Help"));

    

    auto* aboutAction = helpMenu->addAction(themedIcon("help-about", QStyle::SP_MessageBoxInformation), tr("&About"));

    connect(aboutAction, &QAction::triggered, this, [this]() {

        statusBar()->showMessage(tr("Linear Graph Modeling — A CAD-style graph editor"), 3000);

    });

}



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

        addTool(m_modeGroup, toolbar, themedIcon("cursor-arrow", QStyle::SP_FileDialogContentsView),

                tr("Select"), QKeySequence(Qt::Key_Escape), GraphScene::Mode::Select, m_view);

    m_addNodeAction = addTool(m_modeGroup, toolbar, themedIcon("list-add", QStyle::SP_FileDialogNewFolder),

                               tr("Node"), QKeySequence(Qt::Key_N), GraphScene::Mode::AddNode, m_view);

    m_addBranchAction =

        addTool(m_modeGroup, toolbar, themedIcon("draw-line", QStyle::SP_ArrowRight), tr("Branch"),

                QKeySequence(Qt::Key_B), GraphScene::Mode::AddBranch, m_view);

    m_addTwoPortAction = addTool(m_modeGroup, toolbar,

                                 themedIcon("insert-object", QStyle::SP_FileDialogDetailedView),

                                 tr("Two-Port"), QKeySequence(Qt::Key_P), GraphScene::Mode::AddTwoPort,

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
    m_deleteAction->setToolTip(tr("Delete selection (Delete)"));
    connect(m_deleteAction, &QAction::triggered, m_scene, &GraphScene::pushDeleteSelection);

    m_selectAction->setChecked(true);



    toolbar->addSeparator();



    auto* homeAction =

        toolbar->addAction(themedIcon("go-home", QStyle::SP_DirHomeIcon), tr("Home"));

    homeAction->setShortcut(QKeySequence(Qt::Key_Home));

    homeAction->setShortcutContext(Qt::ApplicationShortcut);

    homeAction->setToolTip(tr("Center view on the graph (Home)"));

    connect(homeAction, &QAction::triggered, m_view, &GraphView::goHome);



    auto* zoomInAction =

        toolbar->addAction(themedIcon("zoom-in", QStyle::SP_ArrowUp), tr("Zoom In"));

    zoomInAction->setShortcut(QKeySequence::ZoomIn);

    zoomInAction->setShortcutContext(Qt::ApplicationShortcut);

    zoomInAction->setToolTip(tr("Zoom in (Ctrl++, Shift+scroll)"));

    connect(zoomInAction, &QAction::triggered, m_view, &GraphView::zoomIn);



    auto* zoomOutAction =

        toolbar->addAction(themedIcon("zoom-out", QStyle::SP_ArrowDown), tr("Zoom Out"));

    zoomOutAction->setShortcut(QKeySequence::ZoomOut);

    zoomOutAction->setShortcutContext(Qt::ApplicationShortcut);

    zoomOutAction->setToolTip(tr("Zoom out (Ctrl+-, Shift+scroll)"));

    connect(zoomOutAction, &QAction::triggered, m_view, &GraphView::zoomOut);



    toolbar->addSeparator();



    toolbar->addAction(m_undoAction);

    toolbar->addAction(m_redoAction);



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



void MainWindow::buildStatusBar() {

    auto* hint = new QLabel(modeStatusText(GraphScene::Mode::Select), this);

    hint->setContentsMargins(8, 0, 8, 0);

    statusBar()->addPermanentWidget(hint, 1);



    connect(m_scene, &GraphScene::modeChanged, this, [hint](GraphScene::Mode mode) {

        hint->setText(modeStatusText(mode));

    });

}



void MainWindow::buildDockPanels() {

    m_propertyDock = new QDockWidget(tr("Properties"), this);

    m_propertyDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    

    m_propertyTable = new QTableWidget(0, 2, m_propertyDock);

    m_propertyTable->setHorizontalHeaderLabels({tr("Property"), tr("Value")});

    m_propertyTable->horizontalHeader()->setStretchLastSection(true);

    m_propertyTable->verticalHeader()->setVisible(false);

    m_propertyTable->setAlternatingRowColors(true);

    m_propertyTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    

    m_propertyDock->setWidget(m_propertyTable);

    addDockWidget(Qt::RightDockWidgetArea, m_propertyDock);



    m_objectListDock = new QDockWidget(tr("Objects"), this);

    m_objectListDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    

    m_objectTree = new QTreeWidget(m_objectListDock);

    m_objectTree->setHeaderHidden(true);

    m_objectTree->setAlternatingRowColors(true);

    m_objectTree->setSelectionMode(QAbstractItemView::SingleSelection);

    m_objectTree->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);

    

    m_objectListDock->setWidget(m_objectTree);

    addDockWidget(Qt::LeftDockWidgetArea, m_objectListDock);



    connect(m_scene, &GraphScene::graphChanged, this, &MainWindow::updateObjectList);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::syncObjectTreeSelection);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updatePropertyPanel);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updateFlipBranchAction);

    connect(m_objectTree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onObjectTreeSelectionChanged);

    connect(m_objectTree, &QTreeWidget::itemChanged, this, &MainWindow::onObjectTreeItemChanged);

    connect(m_propertyTable, &QTableWidget::itemChanged, this, &MainWindow::onPropertyTableItemChanged);



    updateObjectList();

    updateFlipBranchAction();

}



void MainWindow::updateObjectList() {

    m_syncingObjectTree = true;

    m_objectTree->clear();



    QList<TwoPortItem*> twoPorts;

    QList<NodeItem*> standaloneNodes;

    QList<BranchItem*> standaloneBranches;



    for (QGraphicsItem* item : m_scene->items()) {

        if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {

            twoPorts.push_back(twoPort);

        }

    }



    for (QGraphicsItem* item : m_scene->items()) {

        if (auto* node = dynamic_cast<NodeItem*>(item)) {

            if (!node->twoPort()) {

                standaloneNodes.push_back(node);

            }

        } else if (auto* branch = dynamic_cast<BranchItem*>(item)) {

            if (TwoPortItem* twoPort = m_scene->twoPortFor(branch)) {

                if (branch == twoPort->leftBranch() || branch == twoPort->rightBranch()) {

                    continue;

                }

            }

            standaloneBranches.push_back(branch);

        }

    }



    auto addEditableItem = [&](QTreeWidgetItem* parent, const QString& label, GraphObjectKind kind,

                               void* ptr) {

        auto* treeItem = parent ? new QTreeWidgetItem(parent, {label}) : new QTreeWidgetItem(m_objectTree, {label});

        setItemObject(treeItem, kind, ptr);

        treeItem->setFlags(treeItem->flags() | Qt::ItemIsEditable);

        return treeItem;

    };



    for (TwoPortItem* twoPort : twoPorts) {

        QTreeWidgetItem* root = addEditableItem(nullptr, twoPort->name(), GraphObjectKind::TwoPort, twoPort);

        addEditableItem(root, twoPort->v1()->name(), GraphObjectKind::Node, twoPort->v1());

        addEditableItem(root, twoPort->v2()->name(), GraphObjectKind::Node, twoPort->v2());

        addEditableItem(root, twoPort->g1()->name(), GraphObjectKind::Node, twoPort->g1());

        if (twoPort->g1() != twoPort->g2()) {
            addEditableItem(root, twoPort->g2()->name(), GraphObjectKind::Node, twoPort->g2());
        }

        addEditableItem(root, twoPort->leftBranch()->name(), GraphObjectKind::Branch, twoPort->leftBranch());

        addEditableItem(root, twoPort->rightBranch()->name(), GraphObjectKind::Branch, twoPort->rightBranch());

        root->setExpanded(true);

    }



    for (NodeItem* node : standaloneNodes) {

        addEditableItem(nullptr, node->name(), GraphObjectKind::Node, node);

    }



    for (BranchItem* branch : standaloneBranches) {

        addEditableItem(nullptr, branch->name(), GraphObjectKind::Branch, branch);

    }



    m_syncingObjectTree = false;

    syncObjectTreeSelection();

}



void MainWindow::onObjectTreeSelectionChanged() {

    if (m_syncingObjectTree) {

        return;

    }

    QTreeWidgetItem* item = m_objectTree->currentItem();

    if (!item || !objectPtr(item)) {

        updatePropertyPanel();

        return;

    }



    m_blockSceneSelectionSync = true;

    m_scene->clearSelection();

    switch (objectKind(item)) {

    case GraphObjectKind::Node: {

        auto* node = static_cast<NodeItem*>(objectPtr(item));

        if (node->twoPort()) {

            m_scene->selectTwoPortNode(node);

        } else {

            node->setSelected(true);

        }

        break;

    }

    case GraphObjectKind::Branch: {

        auto* branch = static_cast<BranchItem*>(objectPtr(item));

        if (TwoPortItem* twoPort = m_scene->twoPortFor(branch)) {

            m_scene->selectTwoPort(twoPort);

        } else {

            branch->setSelected(true);

        }

        break;

    }

    case GraphObjectKind::TwoPort:

        m_scene->selectTwoPort(static_cast<TwoPortItem*>(objectPtr(item)));

        break;

    }

    m_blockSceneSelectionSync = false;

    updatePropertyPanel();

}



void MainWindow::onPropertyTableItemChanged(QTableWidgetItem* item) {

    if (m_updatingPropertyPanel || item->column() != 1 || !m_propertyTargetPtr) {

        return;

    }

    QTableWidgetItem* propItem = m_propertyTable->item(item->row(), 0);

    if (!propItem || propItem->text() != tr("Name")) {

        return;

    }

    const QString name = item->text();

    switch (static_cast<GraphObjectKind>(m_propertyTargetKind)) {

    case GraphObjectKind::Node:

        m_scene->pushSetNodeName(static_cast<NodeItem*>(m_propertyTargetPtr), name);

        break;

    case GraphObjectKind::Branch:

        m_scene->pushSetBranchName(static_cast<BranchItem*>(m_propertyTargetPtr), name);

        break;

    case GraphObjectKind::TwoPort:

        m_scene->pushSetTwoPortName(static_cast<TwoPortItem*>(m_propertyTargetPtr), name);

        break;

    }

}



void MainWindow::onObjectTreeItemChanged(QTreeWidgetItem* item, int column) {

    if (m_syncingObjectTree || column != 0) {

        return;

    }

    void* ptr = objectPtr(item);

    if (!ptr) {

        return;

    }

    const QString name = item->text(0);

    switch (objectKind(item)) {

    case GraphObjectKind::Node:

        m_scene->pushSetNodeName(static_cast<NodeItem*>(ptr), name);

        break;

    case GraphObjectKind::Branch:

        m_scene->pushSetBranchName(static_cast<BranchItem*>(ptr), name);

        break;

    case GraphObjectKind::TwoPort:

        m_scene->pushSetTwoPortName(static_cast<TwoPortItem*>(ptr), name);

        break;

    }

}



void MainWindow::syncObjectTreeSelection() {

    if (m_syncingObjectTree || m_blockSceneSelectionSync) {

        return;

    }



    QGraphicsItem* selected = nullptr;

    GraphObjectKind kind = GraphObjectKind::Node;



    for (QGraphicsItem* item : m_scene->selectedItems()) {

        if (dynamic_cast<TwoPortItem*>(item)) {

            selected = item;

            kind = GraphObjectKind::TwoPort;

            break;

        }

        if (dynamic_cast<BranchItem*>(item)) {

            selected = item;

            kind = GraphObjectKind::Branch;

        } else if (dynamic_cast<NodeItem*>(item) && !selected) {

            selected = item;

            kind = GraphObjectKind::Node;

        }

    }



    m_syncingObjectTree = true;

    if (!selected) {

        m_objectTree->clearSelection();

    } else {

        QTreeWidgetItemIterator it(m_objectTree);

        while (*it) {

            if (objectPtr(*it) == selected && objectKind(*it) == kind) {

                m_objectTree->setCurrentItem(*it);

                break;

            }

            ++it;

        }

    }

    m_syncingObjectTree = false;

}



void MainWindow::updateFlipBranchAction() {

    if (!m_flipBranchAction) {

        return;

    }

    BranchItem* branch = singleSelectedBranch(m_scene);

    if (!branch && m_propertyTargetPtr &&

        m_propertyTargetKind == static_cast<int>(GraphObjectKind::Branch)) {

        branch = static_cast<BranchItem*>(m_propertyTargetPtr);

    }

    m_flipBranchAction->setEnabled(branch != nullptr);

}



void MainWindow::updatePropertyPanel() {

    m_updatingPropertyPanel = true;

    m_propertyTable->setRowCount(0);

    m_propertyTargetPtr = nullptr;

    m_propertyTargetKind = -1;



    auto addRow = [this](const QString& property, const QString& value, bool editable = false) {

        const int row = m_propertyTable->rowCount();

        m_propertyTable->insertRow(row);

        auto* nameItem = new QTableWidgetItem(property);

        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);

        m_propertyTable->setItem(row, 0, nameItem);

        auto* valueItem = new QTableWidgetItem(value);

        if (!editable) {

            valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);

        }

        m_propertyTable->setItem(row, 1, valueItem);

    };



    auto addLabelRow = [this](const QString& property) {

        const int row = m_propertyTable->rowCount();

        m_propertyTable->insertRow(row);

        auto* nameItem = new QTableWidgetItem(property);

        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);

        m_propertyTable->setItem(row, 0, nameItem);

        return row;

    };



    auto addLatexRow = [this, &addLabelRow](const QString& property, const QString& latex) {

        const int row = addLabelRow(property);

        QWidget* widget = lg::createLatexDisplayWidget(latex, m_propertyTable);

        widget->setToolTip(latex);

        m_propertyTable->setCellWidget(row, 1, widget);

        m_propertyTable->resizeRowToContents(row);

    };



    auto appendNormalTreeAnalysis = [this, &addRow](const lg::NormalTreeResult& normalTree) {

        addRow(tr("Analysis"), tr("Normal tree"));

        addRow(tr("Tree branches"), QString::number(normalTree.treeBranches.size()));

        addRow(tr("System order"), QString::number(normalTree.stateVariables.size()));

        if (normalTree.stateVariables.empty()) {

            addRow(tr("State variables"), tr("none"));

            return;

        }

        for (int i = 0; i < static_cast<int>(normalTree.stateVariables.size()); ++i) {

            const lg::NormalTreeResult::StateVariable& state =
                normalTree.stateVariables[static_cast<size_t>(i)];

            const QString label = state.kind == lg::NormalTreeResult::StateVariable::Kind::Across
                                      ? tr("x%1 (across)").arg(i + 1)
                                      : tr("x%1 (through)").arg(i + 1);

            addRow(label, state.symbol);

        }

    };

    auto appendStateSpaceAnalysis = [this, &addRow, &addLatexRow](const lg::StateSpaceResult& stateSpace) {

        if (stateSpace.status != lg::StateSpaceResult::Status::Ok) {

            return;

        }

        addRow(tr("Analysis"), tr("State space"));

        if (!stateSpace.inputs.isEmpty()) {

            const QString inputSummary =
                stateSpace.inputLabels.isEmpty()
                    ? stateSpace.inputs.join(QStringLiteral(", "))
                    : stateSpace.inputLabels.join(QStringLiteral(", "));

            addRow(tr("Inputs"), inputSummary);

        }

        for (const QString& eq : stateSpace.elementalEquations) {

            addRow(tr("Elemental"), eq);

        }

        for (const QString& eq : stateSpace.continuityEquations) {

            addRow(tr("Continuity"), eq);

        }

        for (const QString& eq : stateSpace.compatibilityEquations) {

            addRow(tr("Compatibility"), eq);

        }

        for (const QString& eq : stateSpace.stateEquations) {

            addRow(tr("State equation"), eq);

        }

        if (!stateSpace.matrixForm.isEmpty()) {

            addLatexRow(tr("Matrix form"), stateSpace.matrixForm);

        }

    };



    void* ptr = nullptr;

    GraphObjectKind kind = GraphObjectKind::Node;



    QTreeWidgetItem* treeItem = m_objectTree->currentItem();

    if (treeItem && objectPtr(treeItem)) {

        ptr = objectPtr(treeItem);

        kind = objectKind(treeItem);

    } else {

        TwoPortItem* twoPort = nullptr;

        BranchItem* branch = nullptr;

        NodeItem* node = nullptr;

        for (QGraphicsItem* item : m_scene->selectedItems()) {

            if (auto* tp = dynamic_cast<TwoPortItem*>(item)) {

                twoPort = tp;

                break;

            }

            if (auto* br = dynamic_cast<BranchItem*>(item)) {

                branch = br;

            } else if (auto* nd = dynamic_cast<NodeItem*>(item)) {

                node = nd;

            }

        }

        if (twoPort) {

            ptr = twoPort;

            kind = GraphObjectKind::TwoPort;

        } else if (branch) {

            ptr = branch;

            kind = GraphObjectKind::Branch;

        } else if (node) {

            ptr = node;

            kind = GraphObjectKind::Node;

        }

    }



    if (m_scene->normalTreeHighlightActive()) {

        appendNormalTreeAnalysis(m_scene->lastNormalTreeResult());

    }

    if (m_scene->lastStateSpaceResult().status == lg::StateSpaceResult::Status::Ok) {

        appendStateSpaceAnalysis(m_scene->lastStateSpaceResult());

    }



    if (ptr) {

        m_propertyTargetPtr = ptr;

        m_propertyTargetKind = static_cast<int>(kind);



        switch (kind) {

        case GraphObjectKind::Node: {

            auto* node = static_cast<NodeItem*>(ptr);

            addRow(tr("Name"), node->name(), true);

            if (node->isGround()) {
                addRow(tr("Across variable"), node->acrossVariable());
            } else {
                const int acrossRow = addLabelRow(tr("Across variable"));
                auto* acrossEdit = new QLineEdit(node->acrossVariable(), m_propertyTable);
                connect(acrossEdit, &QLineEdit::editingFinished, this, [this, node, acrossEdit]() {
                    if (m_updatingPropertyPanel) {
                        return;
                    }
                    const QString symbol = acrossEdit->text().trimmed();
                    if (!lg::isValidVariableSymbol(symbol)) {
                        acrossEdit->setText(node->acrossVariable());
                        return;
                    }
                    m_scene->pushSetNodeAcrossVariable(node, symbol);
                });
                m_propertyTable->setCellWidget(acrossRow, 1, acrossEdit);
            }

            const int typeRow = addLabelRow(tr("Node Type"));

            if (node->twoPort()) {

                auto* valueItem = new QTableWidgetItem(

                    node->isGround() ? tr("Reference (Ground) Node") : tr("Normal Node"));

                valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);

                m_propertyTable->setItem(typeRow, 1, valueItem);

            } else {

                auto* combo = new QComboBox(m_propertyTable);

                combo->addItem(tr("Normal Node"));

                combo->addItem(tr("Reference (Ground) Node"));

                combo->setCurrentIndex(node->isGround() ? 1 : 0);

                connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,

                        [this, node](int index) {

                            if (m_updatingPropertyPanel) {

                                return;

                            }

                            m_scene->pushSetNodeGround(node, index == 1);

                        });

                m_propertyTable->setCellWidget(typeRow, 1, combo);

            }

            const QPointF pos = node->scenePos();

            addRow(tr("Position"), QString("(%1, %2)").arg(pos.x(), 0, 'f', 0).arg(pos.y(), 0, 'f', 0));

            addRow(tr("Branches"), QString::number(node->branches().size()));

            if (node->twoPort()) {

                addRow(tr("Parent"), node->twoPort()->name());

            }

            if (!node->isGround()) {
                const int systemTypeRow = addLabelRow(tr("System type"));

                auto* systemTypeCombo = new QComboBox(m_propertyTable);

                systemTypeCombo->addItem(tr("Mechanical (Translational)"),
                                         static_cast<int>(SystemType::Mechanical));

                systemTypeCombo->addItem(tr("Mechanical (Rotational)"),
                                         static_cast<int>(SystemType::MechanicalRotational));

                systemTypeCombo->addItem(tr("Electrical"), static_cast<int>(SystemType::Electrical));

                systemTypeCombo->addItem(tr("Fluid"), static_cast<int>(SystemType::Fluid));

                systemTypeCombo->addItem(tr("Heat"), static_cast<int>(SystemType::Heat));

                systemTypeCombo->setCurrentIndex(
                    systemTypeCombo->findData(static_cast<int>(node->systemType())));

                connect(systemTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,

                        [this, node, systemTypeCombo](int index) {

                            if (m_updatingPropertyPanel) {

                                return;

                            }

                            m_scene->pushSetNodeSystemType(
                                node, static_cast<SystemType>(systemTypeCombo->itemData(index).toInt()));

                            syncDefaultSystemTypeCombo(
                                static_cast<SystemType>(systemTypeCombo->itemData(index).toInt()));

                            updatePropertyPanel();

                        });

                m_propertyTable->setCellWidget(systemTypeRow, 1, systemTypeCombo);
            }

            break;

        }

        case GraphObjectKind::Branch: {

            auto* branch = static_cast<BranchItem*>(ptr);

            addRow(tr("Through variable"), branch->name(), true);

            addRow(tr("Across variable"), lg::branchAcrossVariableText(*branch));

            if (branch->isActive()) {

                addRow(tr("Input"), lg::branchSourceInputDisplay(*branch));

            }

            if (TwoPortItem* twoPort = m_scene->twoPortFor(branch);
                twoPort && GraphScene::isInternalTwoPortBranch(twoPort, branch)) {
                addRow(tr("Parent"), twoPort->name());
                addRow(tr("Through variable"), branch->name());
                addRow(tr("Element"), tr("T-type (port branch)"));
                addRow(tr("Elemental equation"), branch->elementalEquationText());
                addRow(tr("Two-port equations"), twoPort->elementalEquationText());
                addRow(tr("From"), branch->from()->name());
                addRow(tr("To"), branch->to()->name());
                break;
            }

            addRow(tr("System type"), lg::systemTypeLabel(lg::branchSystemType(*branch)));

            const int categoryRow = addLabelRow(tr("Category"));

            auto* categoryCombo = new QComboBox(m_propertyTable);

            categoryCombo->addItem(tr("Passive"), false);

            categoryCombo->addItem(tr("Active"), true);

            categoryCombo->setCurrentIndex(branch->isActive() ? 1 : 0);

            const int elementRow = addLabelRow(tr("Element"));

            auto* elementCombo = new QComboBox(m_propertyTable);

            auto refreshElementOptions = [elementCombo, branch]() {

                const bool isActive = branch->isActive();

                const BranchType current = branch->branchType();

                QSignalBlocker blocker(elementCombo);

                elementCombo->clear();

                elementCombo->addItem(tr("A-type"), static_cast<int>(BranchType::A));

                elementCombo->addItem(tr("T-type"), static_cast<int>(BranchType::T));

                if (!isActive) {

                    elementCombo->addItem(tr("D-type"), static_cast<int>(BranchType::D));

                }

                int idx = 0;

                switch (current) {

                case BranchType::A:

                    idx = 0;

                    break;

                case BranchType::T:

                    idx = 1;

                    break;

                case BranchType::D:

                    idx = isActive ? 0 : 2;

                    break;

                }

                elementCombo->setCurrentIndex(idx);

            };

            refreshElementOptions();

            connect(categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,

                    [this, branch, categoryCombo](int index) {

                        if (m_updatingPropertyPanel) {

                            return;

                        }

                        m_scene->pushSetBranchActive(branch, categoryCombo->itemData(index).toBool());

                    });

            connect(elementCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,

                    [this, branch, elementCombo](int index) {

                        if (m_updatingPropertyPanel) {

                            return;

                        }

                        m_scene->pushSetBranchType(

                            branch, static_cast<BranchType>(elementCombo->itemData(index).toInt()));

                    });

            m_propertyTable->setCellWidget(categoryRow, 1, categoryCombo);

            m_propertyTable->setCellWidget(elementRow, 1, elementCombo);

            if (m_scene->normalTreeHighlightActive() && branch->normalTreeRoleKnown()) {

                addRow(tr("Normal tree"), branch->inNormalTree() ? tr("Tree branch") : tr("Link"));

                if (!branch->isActive()) {
                    if (branch->inNormalTree() && branch->branchType() == BranchType::A) {
                        addRow(tr("State variable"),
                               lg::branchAcrossSymbol(*branch));
                    } else if (!branch->inNormalTree() && branch->branchType() == BranchType::T) {
                        addRow(tr("State variable"), lg::branchFlowSymbol(*branch));
                    }
                }

            }

            if (!branch->isActive()) {
                const QString equation = branch->elementalEquationText();
                if (!equation.isEmpty()) {
                    addRow(tr("Elemental equation"), equation);
                }

                const int constantRow = addLabelRow(tr("Constant"));

                auto* constantEdit = new QLineEdit(branch->elementConstant(), m_propertyTable);

                connect(constantEdit, &QLineEdit::editingFinished, this, [this, branch, constantEdit]() {

                    if (m_updatingPropertyPanel) {

                        return;

                    }

                    m_scene->pushSetBranchConstant(branch, constantEdit->text());

                });

                m_propertyTable->setCellWidget(constantRow, 1, constantEdit);

            }

            addRow(tr("From"), branch->from()->name());

            addRow(tr("To"), branch->to()->name());

            const int flipRow = addLabelRow(tr("Orientation"));

            auto* flipBtn = new QPushButton(tr("Flip"), m_propertyTable);

            connect(flipBtn, &QPushButton::clicked, this, [this, branch]() {

                m_scene->pushFlipBranch(branch);

            });

            m_propertyTable->setCellWidget(flipRow, 1, flipBtn);

            break;

        }

        case GraphObjectKind::TwoPort: {

            auto* twoPort = static_cast<TwoPortItem*>(ptr);

            addRow(tr("Name"), twoPort->name(), true);

            const int kindRow = addLabelRow(tr("Kind"));

            auto* kindCombo = new QComboBox(m_propertyTable);

            kindCombo->addItem(tr("Transformer"), static_cast<int>(TwoPortKind::Transformer));

            kindCombo->addItem(tr("Gyrator"), static_cast<int>(TwoPortKind::Gyrator));

            kindCombo->setCurrentIndex(twoPort->kind() == TwoPortKind::Gyrator ? 1 : 0);

            connect(kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,

                    [this, twoPort, kindCombo](int index) {

                        if (m_updatingPropertyPanel) {

                            return;

                        }

                        m_scene->pushSetTwoPortKind(

                            twoPort, static_cast<TwoPortKind>(kindCombo->itemData(index).toInt()));

                        updatePropertyPanel();

                    });

            m_propertyTable->setCellWidget(kindRow, 1, kindCombo);

            const QString modulusLabel =
                twoPort->kind() == TwoPortKind::Transformer ? tr("Transformer ratio (TF)")
                                                            : tr("Gyrator modulus (GY)");
            const int modulusRow = addLabelRow(modulusLabel);
            auto* modulusEdit = new QLineEdit(twoPort->modulus(), m_propertyTable);
            connect(modulusEdit, &QLineEdit::editingFinished, this, [this, twoPort, modulusEdit]() {
                if (m_updatingPropertyPanel) {
                    return;
                }
                m_scene->pushSetTwoPortModulus(twoPort, modulusEdit->text());
                updatePropertyPanel();
            });
            m_propertyTable->setCellWidget(modulusRow, 1, modulusEdit);

            addRow(tr("Elemental equations"), twoPort->elementalEquationText());

            if (twoPort->hasSharedReference()) {
                addRow(tr("Reference"), tr("Shared (3-terminal)"));
            }

            addRow(tr("Port 1"), QStringLiteral("v\u2081 = %1, f\u2081 = %2")
                                      .arg(twoPort->v1()->acrossVariable(), twoPort->leftBranch()->name()));
            addRow(tr("Port 2"), QStringLiteral("v\u2082 = %1, f\u2082 = %2")
                                      .arg(twoPort->v2()->acrossVariable(), twoPort->rightBranch()->name()));

            const QPointF center = twoPort->center();

            addRow(tr("Center"), QString("(%1, %2)").arg(center.x(), 0, 'f', 0).arg(center.y(), 0, 'f', 0));

            break;

        }

        }

    } else if (m_objectTree->topLevelItemCount() == 0) {

        addRow(tr("Type"), tr("Graph Scene"));

        addRow(tr("Zoom"), QString::number(m_view->transform().m11(), 'f', 2));

        addRow(tr("Grid"), tr("Enabled"));

    }

    m_updatingPropertyPanel = false;

    updateFlipBranchAction();

}



void MainWindow::syncModeUi(GraphScene::Mode mode) {

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
    return s;
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
    m_scene->refreshAppearance();
    refreshChromeTheme();
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

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmDiscardChanges()) {
        event->ignore();
        return;
    }
    event->accept();
}

void MainWindow::updateWindowTitle() {
    const QString name =
        m_currentFilePath.isEmpty() ? tr("Untitled") : QFileInfo(m_currentFilePath).fileName();
    setWindowTitle(tr("%1 [*] - Linear Graph Modeling").arg(name));
    setWindowModified(!m_scene->undoStack()->isClean());
}

bool MainWindow::confirmDiscardChanges() {
    if (m_scene->undoStack()->isClean()) {
        return true;
    }
    const QMessageBox::StandardButton btn = QMessageBox::warning(
        this, tr("Unsaved Changes"),
        tr("The document has been modified.\nDo you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (btn == QMessageBox::Cancel) {
        return false;
    }
    if (btn == QMessageBox::Save) {
        return fileSave();
    }
    return true;
}

void MainWindow::fileNew() {
    if (!confirmDiscardChanges()) {
        return;
    }
    m_scene->clearDocument();
    m_currentFilePath.clear();
    m_scene->undoStack()->setClean();
    updateWindowTitle();
    updateObjectList();
    updatePropertyPanel();
}

void MainWindow::fileOpen() {
    if (!confirmDiscardChanges()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open"), QString(), tr("Linear Graph Model (*.lgm);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open Failed"), tr("Could not read %1.").arg(path));
        return;
    }

    QString error;
    if (!m_scene->documentFromJson(file.readAll(), &error)) {
        QMessageBox::warning(this, tr("Open Failed"), error);
        return;
    }

    m_currentFilePath = path;
    m_scene->undoStack()->setClean();
    syncDefaultSystemTypeCombo(m_scene->defaultSystemType());
    updateWindowTitle();
    updateObjectList();
    updatePropertyPanel();
}

bool MainWindow::fileSave() {
    if (m_currentFilePath.isEmpty()) {
        return fileSaveAs();
    }
    return writeDocument(m_currentFilePath);
}

bool MainWindow::fileSaveAs() {
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save As"), m_currentFilePath.isEmpty() ? QString() : m_currentFilePath,
        tr("Linear Graph Model (*.lgm);;All Files (*)"));
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QStringLiteral(".lgm"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".lgm");
    }
    if (!writeDocument(path)) {
        return false;
    }
    m_currentFilePath = path;
    updateWindowTitle();
    return true;
}

bool MainWindow::writeDocument(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Save Failed"), tr("Could not write %1.").arg(path));
        return false;
    }
    if (file.write(m_scene->documentToJson()) == -1) {
        QMessageBox::warning(this, tr("Save Failed"), tr("Could not write %1.").arg(path));
        return false;
    }
    m_scene->undoStack()->setClean();
    updateWindowTitle();
    return true;
}
