#include "mainwindow.h"



#include "canvas.h"



#include <QActionGroup>

#include <QApplication>

#include <QDockWidget>

#include <QHeaderView>

#include <QLabel>

#include <QListWidget>

#include <QTreeWidget>

#include <QTreeWidgetItemIterator>

#include <QMenuBar>

#include <QStatusBar>

#include <QStyle>

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



    connect(m_scene, &GraphScene::modeChanged, this, &MainWindow::syncModeUi);



    setWindowTitle(tr("Linear Graph Modeling"));

    resize(1200, 800);

    

    setDockNestingEnabled(true);

}



void MainWindow::buildMenuBar() {

    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    

    auto* newAction = fileMenu->addAction(themedIcon("document-new", QStyle::SP_FileIcon), tr("&New"));

    newAction->setShortcut(QKeySequence::New);

    

    auto* openAction = fileMenu->addAction(themedIcon("document-open", QStyle::SP_DirIcon), tr("&Open..."));

    openAction->setShortcut(QKeySequence::Open);

    

    auto* saveAction = fileMenu->addAction(themedIcon("document-save", QStyle::SP_DialogSaveButton), tr("&Save"));

    saveAction->setShortcut(QKeySequence::Save);

    

    auto* saveAsAction = fileMenu->addAction(themedIcon("document-save-as", QStyle::SP_DialogSaveButton), tr("Save &As..."));

    saveAsAction->setShortcut(QKeySequence::SaveAs);

    

    fileMenu->addSeparator();

    

    auto* exitAction = fileMenu->addAction(themedIcon("application-exit", QStyle::SP_DialogCloseButton), tr("E&xit"));

    exitAction->setShortcut(QKeySequence::Quit);

    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);



    auto* editMenu = menuBar()->addMenu(tr("&Edit"));

    editMenu->addAction(m_undoAction);

    editMenu->addAction(m_redoAction);

    editMenu->addSeparator();

    

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

    toolbar->setStyleSheet("QToolBar { spacing: 4px; padding: 4px; background: palette(window); border-bottom: 1px solid palette(mid); }");



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

    m_deleteAction = addTool(m_modeGroup, toolbar, themedIcon("edit-delete", QStyle::SP_TrashIcon),

                             tr("Delete"), QKeySequence(Qt::Key_D), GraphScene::Mode::Delete, m_view);

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

    connect(m_objectTree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onObjectTreeSelectionChanged);

    connect(m_objectTree, &QTreeWidget::itemChanged, this, &MainWindow::onObjectTreeItemChanged);



    updateObjectList();

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

        addEditableItem(root, twoPort->g2()->name(), GraphObjectKind::Node, twoPort->g2());

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



    m_scene->clearSelection();

    switch (objectKind(item)) {

    case GraphObjectKind::Node: {

        auto* node = static_cast<NodeItem*>(objectPtr(item));

        if (node->twoPort()) {

            m_scene->selectTwoPort(node->twoPort());

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

    updatePropertyPanel();

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

        static_cast<NodeItem*>(ptr)->setName(name);

        break;

    case GraphObjectKind::Branch:

        static_cast<BranchItem*>(ptr)->setName(name);

        break;

    case GraphObjectKind::TwoPort:

        static_cast<TwoPortItem*>(ptr)->setName(name);

        break;

    }

    updatePropertyPanel();

}



void MainWindow::syncObjectTreeSelection() {

    if (m_syncingObjectTree) {

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



void MainWindow::updatePropertyPanel() {

    m_propertyTable->setRowCount(0);



    auto addRow = [this](const QString& property, const QString& value, bool editable = false) {

        const int row = m_propertyTable->rowCount();

        m_propertyTable->insertRow(row);

        m_propertyTable->setItem(row, 0, new QTableWidgetItem(property));

        auto* valueItem = new QTableWidgetItem(value);

        if (!editable) {

            valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);

        }

        m_propertyTable->setItem(row, 1, valueItem);

    };



    auto addReadOnlyRows = [this]() {

        for (int row = 0; row < m_propertyTable->rowCount(); ++row) {

            if (auto* item = m_propertyTable->item(row, 0)) {

                item->setFlags(item->flags() & ~Qt::ItemIsEditable);

            }

        }

    };



    QTreeWidgetItem* treeItem = m_objectTree->currentItem();

    if (treeItem && objectPtr(treeItem)) {

        void* ptr = objectPtr(treeItem);

        switch (objectKind(treeItem)) {

        case GraphObjectKind::Node: {

            auto* node = static_cast<NodeItem*>(ptr);

            addRow(tr("Type"), node->isGround() ? tr("Ground Node") : tr("Node"));

            addRow(tr("Name"), node->name(), true);

            const QPointF pos = node->scenePos();

            addRow(tr("Position"), QString("(%1, %2)").arg(pos.x(), 0, 'f', 0).arg(pos.y(), 0, 'f', 0));

            addRow(tr("Branches"), QString::number(node->branches().size()));

            if (node->twoPort()) {

                addRow(tr("Parent"), node->twoPort()->name());

            }

            break;

        }

        case GraphObjectKind::Branch: {

            auto* branch = static_cast<BranchItem*>(ptr);

            addRow(tr("Type"), tr("Branch"));

            addRow(tr("Name"), branch->name(), true);

            const QString typeLabel = branch->isActive() ? tr("Active") : tr("Passive");

            addRow(tr("Category"), typeLabel);

            QString elementType;

            switch (branch->branchType()) {

            case BranchType::A: elementType = tr("A-type"); break;

            case BranchType::T: elementType = tr("T-type"); break;

            case BranchType::D: elementType = tr("D-type"); break;

            }

            addRow(tr("Element"), elementType);

            addRow(tr("From"), branch->from()->name());

            addRow(tr("To"), branch->to()->name());

            break;

        }

        case GraphObjectKind::TwoPort: {

            auto* twoPort = static_cast<TwoPortItem*>(ptr);

            addRow(tr("Type"),

                   twoPort->kind() == TwoPortKind::Transformer ? tr("Transformer") : tr("Gyrator"));

            addRow(tr("Name"), twoPort->name(), true);

            const QPointF center = twoPort->center();

            addRow(tr("Center"), QString("(%1, %2)").arg(center.x(), 0, 'f', 0).arg(center.y(), 0, 'f', 0));

            break;

        }

        }

        addReadOnlyRows();

        return;

    }



    if (m_objectTree->topLevelItemCount() == 0) {

        addRow(tr("Type"), tr("Graph Scene"));

        addRow(tr("Zoom"), QString::number(m_view->transform().m11(), 'f', 2));

        addRow(tr("Grid"), tr("Enabled"));

        addReadOnlyRows();

    }

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

    case GraphScene::Mode::Delete:

        m_deleteAction->setChecked(true);

        break;

    }

}



QString MainWindow::modeStatusText(GraphScene::Mode mode) {

    switch (mode) {

    case GraphScene::Mode::Select:

        return tr("Select — drag to move; double-click node/branch for properties, two-port or press T to switch transformer/gyrator");

    case GraphScene::Mode::AddNode:

        return tr("Add node — click empty grid to place a node");

    case GraphScene::Mode::AddBranch:

        return tr("Add branch — click start node, then end node");

    case GraphScene::Mode::AddTwoPort:

        return tr("Add two-port — click empty grid; two branches, four nodes (V₁/V₂ top, ground below)");

    case GraphScene::Mode::Delete:

        return tr("Delete — click a node, branch, or two-port to remove it");

    }

    return {};

}

