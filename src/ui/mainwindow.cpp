#include "mainwindow.h"



#include "analyze_window.h"
#include "app_log.h"
#include "app_shortcuts.h"
#include "app_update.h"
#include "canvas.h"
#include "guide_window.h"
#include "settings_window.h"
#include "tool_icons.h"

#include "elemental_equation.h"
#include "state_space.h"



#include <QActionGroup>

#include <QApplication>

#include <QCoreApplication>

#include <QCloseEvent>

#include <QComboBox>

#include <QDate>

#include <QDir>

#include <QDockWidget>

#include <QFile>

#include <QFileDialog>

#include <QFileInfo>

#include <QFont>

#include <QFrame>

#include <QGuiApplication>

#include <QHeaderView>

#include <QInputDialog>

#include <QKeyEvent>

#include <QLabel>

#include <QLineEdit>

#include <QListWidget>

#include <QTreeWidget>

#include <QTreeWidgetItemIterator>

#include <QWidget>

#include <QMenu>

#include <QMenuBar>

#include <QMessageBox>

#include <QSignalBlocker>

#include <QHBoxLayout>

#include <QPushButton>

#include <QShortcut>

#include <QScrollArea>

#include <QScrollBar>

#include <QSet>

#include <QStatusBar>

#include <QStyle>

#include <QStyleHints>

#include <QFontDatabase>

#include <QTableWidget>

#include <QTextEdit>

#include <QToolBar>

#include <QVBoxLayout>

#include <QVector>

#include <algorithm>



namespace {



enum class GraphObjectKind { Node, Branch, TwoPort };

constexpr int kObjectPtrRole = Qt::UserRole;

constexpr int kObjectKindRole = Qt::UserRole + 1;

constexpr int kCategoryRole = Qt::UserRole + 2;

constexpr int kKindLabelRole = Qt::UserRole + 3;

constexpr int kParentTwoPortRole = Qt::UserRole + 4;



void setItemObject(QTreeWidgetItem* item, GraphObjectKind kind, void* ptr) {

    item->setData(0, kObjectPtrRole, reinterpret_cast<quintptr>(ptr));

    item->setData(0, kObjectKindRole, static_cast<int>(kind));

}

void setItemParentTwoPort(QTreeWidgetItem* item, TwoPortItem* twoPort) {

    item->setData(0, kParentTwoPortRole, reinterpret_cast<quintptr>(twoPort));

}

TwoPortItem* itemParentTwoPort(const QTreeWidgetItem* item) {

    return reinterpret_cast<TwoPortItem*>(item->data(0, kParentTwoPortRole).value<quintptr>());

}



void* objectPtr(const QTreeWidgetItem* item) {

    return reinterpret_cast<void*>(item->data(0, kObjectPtrRole).value<quintptr>());

}



GraphObjectKind objectKind(const QTreeWidgetItem* item) {

    return static_cast<GraphObjectKind>(item->data(0, kObjectKindRole).toInt());

}

QString objectExpansionKey(GraphObjectKind kind, void* ptr) {
    return QStringLiteral("obj:%1:%2")
        .arg(static_cast<int>(kind))
        .arg(reinterpret_cast<quintptr>(ptr));
}

QString categoryExpansionKey(const QString& id) {
    return QStringLiteral("cat:%1").arg(id);
}

QIcon objectListIcon(GraphObjectKind kind) {
    switch (kind) {
    case GraphObjectKind::Node:
        return ToolIcons::node();
    case GraphObjectKind::Branch:
        return ToolIcons::branch();
    case GraphObjectKind::TwoPort:
        return ToolIcons::twoPort();
    }
    return {};
}

QString twoPortKindLabel(TwoPortKind kind) {
    return kind == TwoPortKind::Transformer ? QObject::tr("Transformer")
                                            : QObject::tr("Gyrator");
}

QString objectKindLabel(GraphObjectKind kind) {
    switch (kind) {
    case GraphObjectKind::Node:
        return QObject::tr("Node");
    case GraphObjectKind::Branch:
        return QObject::tr("Branch");
    case GraphObjectKind::TwoPort:
        return QObject::tr("Two-port");
    }
    return {};
}

QString examplesDir() {
    const QString besideExe =
        QCoreApplication::applicationDirPath() + QStringLiteral("/Examples");
    if (QDir(besideExe).exists()) {
        return besideExe;
    }
#ifdef EXAMPLES_SOURCE_DIR
    const QString fromSource = QStringLiteral(EXAMPLES_SOURCE_DIR);
    if (QDir(fromSource).exists()) {
        return fromSource;
    }
#endif
    return {};
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



struct GraphSelectionEntry {
    void* ptr = nullptr;
    GraphObjectKind kind = GraphObjectKind::Node;
};

QString objectLabel(GraphObjectKind kind, void* ptr) {
    switch (kind) {
    case GraphObjectKind::Node:
        return static_cast<NodeItem*>(ptr)->name();
    case GraphObjectKind::Branch:
        return static_cast<BranchItem*>(ptr)->name();
    case GraphObjectKind::TwoPort:
        return static_cast<TwoPortItem*>(ptr)->name();
    }
    return {};
}

QVector<GraphSelectionEntry> primarySceneSelection(const GraphScene* scene) {
    QVector<GraphSelectionEntry> entries;
    QSet<const void*> seen;

    auto add = [&](void* ptr, GraphObjectKind kind) {
        if (!ptr || seen.contains(ptr)) {
            return;
        }
        seen.insert(ptr);
        entries.push_back({ptr, kind});
    };

    for (QGraphicsItem* item : scene->selectedItems()) {
        if (dynamic_cast<TwoPortItem*>(item)) {
            add(item, GraphObjectKind::TwoPort);
        }
    }

    for (QGraphicsItem* item : scene->selectedItems()) {
        if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            if (const TwoPortItem* twoPort = scene->twoPortFor(branch)) {
                if (seen.contains(twoPort)) {
                    continue;
                }
            }
            add(branch, GraphObjectKind::Branch);
        } else if (auto* node = dynamic_cast<NodeItem*>(item)) {
            bool coveredBySelectedTwoPort = false;
            for (QGraphicsItem* other : scene->selectedItems()) {
                if (auto* twoPort = dynamic_cast<TwoPortItem*>(other)) {
                    if (GraphScene::isTwoPortPortNode(twoPort, node)) {
                        coveredBySelectedTwoPort = true;
                        break;
                    }
                }
            }
            if (!coveredBySelectedTwoPort) {
                add(node, GraphObjectKind::Node);
            }
        }
    }

    return entries;
}

BranchItem* singleSelectedBranch(const GraphScene* scene) {
    const QVector<GraphSelectionEntry> entries = primarySceneSelection(scene);
    if (entries.size() != 1 || entries.front().kind != GraphObjectKind::Branch) {
        return nullptr;
    }
    return static_cast<BranchItem*>(entries.front().ptr);
}

QTreeWidgetItem* pickObjectTreeItem(const QVector<GraphSelectionEntry>& entries,
                                    const GraphSelectionEntry& entry, QTreeWidget* tree,
                                    const GraphScene* scene) {
    QVector<QTreeWidgetItem*> candidates;
    QTreeWidgetItemIterator it(tree);
    while (*it) {
        if (objectPtr(*it) == entry.ptr && objectKind(*it) == entry.kind) {
            candidates.push_back(*it);
        }
        ++it;
    }
    if (candidates.isEmpty()) {
        return nullptr;
    }
    if (candidates.size() == 1 || entry.kind != GraphObjectKind::Node) {
        return candidates.front();
    }

    TwoPortItem* context = nullptr;
    for (const GraphSelectionEntry& other : entries) {
        if (other.kind != GraphObjectKind::TwoPort) {
            continue;
        }
        auto* twoPort = static_cast<TwoPortItem*>(other.ptr);
        if (GraphScene::isTwoPortPortNode(twoPort, static_cast<NodeItem*>(entry.ptr))) {
            context = twoPort;
            break;
        }
    }
    if (!context) {
        context = scene->twoPortForNode(static_cast<NodeItem*>(entry.ptr));
    }
    for (QTreeWidgetItem* item : candidates) {
        if (itemParentTwoPort(item) == context) {
            return item;
        }
    }
    return candidates.front();
}



QIcon themedIcon(const char* name, QStyle::StandardPixmap fallback) {

    QIcon icon = QIcon::fromTheme(name);

    if (icon.isNull()) {

        icon = QApplication::style()->standardIcon(fallback);

    }

    return icon;

}



QAction* addTool(QActionGroup* group, QToolBar* toolbar, const QIcon& icon, const QString& text,

                 const char* shortcutId, GraphScene::Mode mode, GraphView* view) {

    auto* action = toolbar->addAction(icon, text);

    action->setCheckable(true);

    action->setObjectName(QString::fromLatin1(shortcutId));

    action->setShortcutContext(Qt::ApplicationShortcut);

    action->setData(static_cast<int>(mode));

    group->addAction(action);

    QObject::connect(action, &QAction::triggered, view, [view, mode] { view->setToolMode(mode); });

    return action;

}

QString shortcutToolTip(const QString& label, const QKeySequence& shortcut) {
    if (shortcut.isEmpty()) {
        return label;
    }
    return QObject::tr("%1 (%2)").arg(label, shortcut.toString(QKeySequence::NativeText));
}

}  // namespace



MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {

    m_scene = new GraphScene(this);

    m_view = new GraphView(m_scene, this);



    m_view->setRenderHint(QPainter::Antialiasing);

    setCentralWidget(m_view);

    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);

    

    m_undoAction = new QAction(themedIcon("edit-undo", QStyle::SP_ArrowBack), tr("Undo"), this);

    m_undoAction->setObjectName(QStringLiteral("edit.undo"));

    m_undoAction->setShortcutContext(Qt::ApplicationShortcut);

    m_undoAction->setEnabled(false);

    connect(m_undoAction, &QAction::triggered, m_scene, &GraphScene::undo);



    m_redoAction = new QAction(themedIcon("edit-redo", QStyle::SP_ArrowForward), tr("Redo"), this);

    m_redoAction->setObjectName(QStringLiteral("edit.redo"));

    m_redoAction->setShortcutContext(Qt::ApplicationShortcut);

    m_redoAction->setEnabled(false);

    connect(m_redoAction, &QAction::triggered, m_scene, &GraphScene::redo);



    m_toggleTwoPortAction = new QAction(this);
    m_toggleTwoPortAction->setObjectName(QStringLiteral("edit.toggleTwoPort"));
    m_toggleTwoPortAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_toggleTwoPortAction, &QAction::triggered, this, [this]() {
        if (m_scene->mode() != GraphScene::Mode::Select) {
            return;
        }
        for (QGraphicsItem* item : m_scene->selectedItems()) {
            if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
                m_scene->pushToggleTwoPortKind(twoPort);
                updatePropertyPanel();
                return;
            }
            if (auto* node = dynamic_cast<NodeItem*>(item)) {
                if (TwoPortItem* twoPort = node->twoPort()) {
                    m_scene->pushToggleTwoPortKind(twoPort);
                    updatePropertyPanel();
                    return;
                }
            }
        }
    });
    addAction(m_toggleTwoPortAction);



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

        if (m_stateSpaceDock) {

            m_stateSpaceDock->setVisible(checked);

        }

    });

    

    viewMenu->addSeparator();

    

    auto* resetLayoutAction = viewMenu->addAction(tr("&Reset Layout"));

    connect(resetLayoutAction, &QAction::triggered, this, [this]() {

        removeDockWidget(m_propertyDock);

        removeDockWidget(m_objectListDock);

        removeDockWidget(m_analyzeDock);

        removeDockWidget(m_stateSpaceDock);

        removeDockWidget(m_consoleDock);

        addDockWidget(Qt::LeftDockWidgetArea, m_objectListDock);

        addDockWidget(Qt::RightDockWidgetArea, m_propertyDock);

        addDockWidget(Qt::RightDockWidgetArea, m_analyzeDock);

        tabifyDockWidget(m_propertyDock, m_analyzeDock);

        addDockWidget(Qt::BottomDockWidgetArea, m_stateSpaceDock);

        addDockWidget(Qt::BottomDockWidgetArea, m_consoleDock);

        tabifyDockWidget(m_stateSpaceDock, m_consoleDock);

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
               "<p>Copyright &copy; %4 Furkan Karaketir</p>"
               "<p><a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">%5</a></p>")
                .arg(tr("Linear graph modeling and state-space analysis."),
                     tr("Version %1").arg(QApplication::applicationVersion()),
                     tr("This program is free software; you can redistribute it and/or modify "
                        "it under the terms of the GNU General Public License as published by "
                        "the Free Software Foundation, either version 3 of the License, or "
                        "(at your option) any later version."),
                     QString::number(QDate::currentDate().year()),
                     tr("GNU General Public License v3.0")));
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

    m_objectListDock->setObjectName(QStringLiteral("objectListDock"));

    m_objectListDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    

    m_objectTree = new QTreeWidget(m_objectListDock);

    m_objectTree->setColumnCount(2);

    m_objectTree->setHeaderLabels({tr("Name"), tr("Kind")});

    m_objectTree->header()->setStretchLastSection(false);

    m_objectTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    m_objectTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    m_objectTree->setAlternatingRowColors(true);

    m_objectTree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    m_objectTree->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_objectTree->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);

    m_objectTree->setUniformRowHeights(true);

    m_objectTree->setRootIsDecorated(true);

    

    m_objectListDock->setWidget(m_objectTree);

    addDockWidget(Qt::LeftDockWidgetArea, m_objectListDock);



    m_analyzeDock = new QDockWidget(tr("Analyze"), this);
    m_analyzeDock->setObjectName(QStringLiteral("analyzeDock"));
    m_analyzeDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_analyzePanel = new AnalyzeWindow(m_scene, m_view, m_analyzeDock);
    m_analyzePanel->setRefreshCallback([this]() { updatePropertyPanel(); });
    connect(m_analyzePanel, &AnalyzeWindow::stateSpaceComputed, this, &MainWindow::updateStateSpacePanel);
    m_analyzeDock->setWidget(m_analyzePanel);
    addDockWidget(Qt::RightDockWidgetArea, m_analyzeDock);
    tabifyDockWidget(m_propertyDock, m_analyzeDock);
    m_analyzeDock->hide();

    if (auto* panelAction = findChild<QAction*>(QStringLiteral("view.analyzePanel"))) {
        connect(m_analyzeDock, &QDockWidget::visibilityChanged, panelAction, &QAction::setChecked);
    }

    m_consoleDock = new QDockWidget(tr("Console"), this);
    m_consoleDock->setObjectName(QStringLiteral("consoleDock"));
    m_consoleDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    m_consoleDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    m_consoleDock->setMinimumHeight(120);

    auto* consolePanel = new QWidget(m_consoleDock);
    auto* consoleLayout = new QVBoxLayout(consolePanel);
    consoleLayout->setContentsMargins(4, 4, 4, 4);
    consoleLayout->setSpacing(4);

    m_consoleText = new QTextEdit(consolePanel);
    m_consoleText->setReadOnly(true);
    m_consoleText->setLineWrapMode(QTextEdit::NoWrap);
    m_consoleText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    for (const QString& line : AppLog::instance().lines()) {
        m_consoleText->append(line);
    }
    connect(&AppLog::instance(), &AppLog::lineAppended, m_consoleText, [this](const QString& line) {
        m_consoleText->append(line);
        const QScrollBar* bar = m_consoleText->verticalScrollBar();
        if (bar && bar->value() >= bar->maximum() - 2) {
            m_consoleText->verticalScrollBar()->setValue(m_consoleText->verticalScrollBar()->maximum());
        }
    });

    auto* clearBtn = new QPushButton(tr("Clear"), consolePanel);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        AppLog::instance().clear();
        m_consoleText->clear();
    });

    consoleLayout->addWidget(m_consoleText);
    auto* consoleButtons = new QHBoxLayout;
    consoleButtons->addStretch();
    consoleButtons->addWidget(clearBtn);
    consoleLayout->addLayout(consoleButtons);

    m_consoleDock->setWidget(consolePanel);

    m_stateSpaceDock = new QDockWidget(tr("State Space"), this);

    m_stateSpaceDock->setObjectName(QStringLiteral("stateSpaceDock"));

    m_stateSpaceDock->setAllowedAreas(Qt::BottomDockWidgetArea);

    m_stateSpaceDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    m_stateSpaceDock->setMinimumHeight(120);



    auto* stateSpaceScroll = new QScrollArea(m_stateSpaceDock);

    stateSpaceScroll->setWidgetResizable(true);

    stateSpaceScroll->setFrameShape(QFrame::NoFrame);

    stateSpaceScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);



    m_stateSpaceScrollContent = new QWidget(stateSpaceScroll);

    m_stateSpaceLayout = new QVBoxLayout(m_stateSpaceScrollContent);

    m_stateSpaceLayout->setContentsMargins(8, 8, 8, 8);

    m_stateSpaceLayout->setSpacing(4);

    stateSpaceScroll->setWidget(m_stateSpaceScrollContent);

    m_stateSpaceDock->setWidget(stateSpaceScroll);

    addDockWidget(Qt::BottomDockWidgetArea, m_stateSpaceDock);

    addDockWidget(Qt::BottomDockWidgetArea, m_consoleDock);

    tabifyDockWidget(m_stateSpaceDock, m_consoleDock);

    m_stateSpaceDock->hide();

    m_consoleDock->hide();



    if (auto* panelAction = findChild<QAction*>(QStringLiteral("view.stateSpacePanel"))) {

        connect(m_stateSpaceDock, &QDockWidget::visibilityChanged, panelAction, &QAction::setChecked);

    }



    connect(m_scene, &GraphScene::graphChanged, this, &MainWindow::updateObjectList);

    connect(m_scene, &GraphScene::normalTreeHighlightChanged, this,
            &MainWindow::updatePropertyPanel);

    connect(m_scene, &GraphScene::manualNormalTreeValidationChanged, this,
            [this](const lg::NormalTreeResult& result) {
                if (m_scene->mode() != GraphScene::Mode::SelectNormalTree) {
                    return;
                }
                updatePropertyPanel();
                if (result.status == lg::NormalTreeResult::Status::Ok) {
                    QStringList stateVarText;
                    stateVarText.reserve(static_cast<int>(result.stateVariables.size()));
                    for (const lg::NormalTreeResult::StateVariable& state : result.stateVariables) {
                        stateVarText.push_back(state.symbol);
                    }
                    const QString stateVarSummary =
                        stateVarText.isEmpty() ? tr("none") : stateVarText.join(QStringLiteral(", "));
                    statusBar()->showMessage(
                        tr("Manual normal tree — valid, order %1: %2")
                            .arg(result.stateVariables.size())
                            .arg(stateVarSummary),
                        0);
                    return;
                }
                statusBar()->showMessage(
                    result.message.isEmpty()
                        ? tr("Manual normal tree — invalid selection.")
                        : result.message,
                    0);
            });

    connect(m_scene, &GraphScene::manualNormalTreeRejected, this,
            [this](const QString& message) {
                QMessageBox::warning(this, tr("Normal Tree"), message);
                statusBar()->showMessage(tr("Manual normal tree selection discarded."), 4000);
                updatePropertyPanel();
            });

    connect(m_scene, &GraphScene::manualNormalTreeAccepted, this,
            [this](const lg::NormalTreeResult& result) {
                QStringList stateVarText;
                for (const lg::NormalTreeResult::StateVariable& state : result.stateVariables) {
                    stateVarText.push_back(state.symbol);
                }
                const QString stateVarSummary =
                    stateVarText.isEmpty() ? tr("none") : stateVarText.join(QStringLiteral(", "));
                statusBar()->showMessage(
                    tr("Manual normal tree applied — %1 branches, order %2: %3.")
                        .arg(result.treeBranches.size())
                        .arg(result.stateVariables.size())
                        .arg(stateVarSummary),
                    8000);
                m_view->setToolMode(GraphScene::Mode::Select);
                updatePropertyPanel();
            });

    const auto applyManualTree = [this]() {
        if (m_scene->mode() != GraphScene::Mode::SelectNormalTree) {
            return;
        }
        m_scene->acceptManualNormalTreeSelection();
        m_view->setToolMode(GraphScene::Mode::Select);
    };

    m_applyManualTreeReturnShortcut =
        new QShortcut(QKeySequence(Qt::Key_Return), this, nullptr, nullptr, Qt::ApplicationShortcut);
    m_applyManualTreeReturnShortcut->setEnabled(false);
    connect(m_applyManualTreeReturnShortcut, &QShortcut::activated, this, applyManualTree);

    m_applyManualTreeEnterShortcut =
        new QShortcut(QKeySequence(Qt::Key_Enter), this, nullptr, nullptr, Qt::ApplicationShortcut);
    m_applyManualTreeEnterShortcut->setEnabled(false);
    connect(m_applyManualTreeEnterShortcut, &QShortcut::activated, this, applyManualTree);

    m_cancelManualTreeShortcut =
        new QShortcut(QKeySequence(Qt::Key_Escape), this, nullptr, nullptr, Qt::ApplicationShortcut);
    m_cancelManualTreeShortcut->setEnabled(false);
    connect(m_cancelManualTreeShortcut, &QShortcut::activated, this, [this]() {
        m_scene->cancelManualNormalTreeSelection();
        m_view->setToolMode(GraphScene::Mode::Select);
        statusBar()->showMessage(tr("Manual normal tree selection cancelled."), 3000);
        updatePropertyPanel();
    });

    connect(m_scene, &GraphScene::graphChanged, this, &MainWindow::updateStateSpacePanel);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::syncObjectTreeSelection);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updatePropertyPanel);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updateFlipBranchAction);

    connect(m_objectTree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onObjectTreeSelectionChanged);

    connect(m_objectTree, &QTreeWidget::itemChanged, this, &MainWindow::onObjectTreeItemChanged);

    connect(m_objectTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        void* ptr = objectPtr(item);
        if (!ptr) {
            return;
        }
        QGraphicsItem* gfx = nullptr;
        switch (objectKind(item)) {
        case GraphObjectKind::Node:
            gfx = static_cast<NodeItem*>(ptr);
            break;
        case GraphObjectKind::Branch:
            gfx = static_cast<BranchItem*>(ptr);
            break;
        case GraphObjectKind::TwoPort:
            gfx = static_cast<TwoPortItem*>(ptr);
            break;
        }
        if (gfx) {
            m_view->centerOn(gfx);
        }
    });

    connect(m_propertyTable, &QTableWidget::itemChanged, this, &MainWindow::onPropertyTableItemChanged);



    updateObjectList();

    updateFlipBranchAction();

    updateStateSpacePanel();

}



void MainWindow::updateObjectList() {

    if (m_clearingDocument) {

        return;

    }

    QSet<QString> expanded;

  {
        QTreeWidgetItemIterator it(m_objectTree);

        while (*it) {

            if ((*it)->isExpanded()) {

                if (void* ptr = objectPtr(*it)) {

                    expanded.insert(objectExpansionKey(objectKind(*it), ptr));

                } else if (const QString catId = (*it)->data(0, kCategoryRole).toString(); !catId.isEmpty()) {

                    expanded.insert(categoryExpansionKey(catId));

                }

            }

            ++it;

        }

    }



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



    const auto byName = [](auto* a, auto* b) {
        return a->name().compare(b->name(), Qt::CaseInsensitive) < 0;
    };

    std::sort(twoPorts.begin(), twoPorts.end(), byName);

    std::sort(standaloneNodes.begin(), standaloneNodes.end(), byName);

    std::sort(standaloneBranches.begin(), standaloneBranches.end(), byName);



    auto addCategory = [&](const QString& id, const QString& title, int count) -> QTreeWidgetItem* {

        const QString label =

            count > 0 ? tr("%1 (%2)").arg(title).arg(count) : title;

        auto* cat = new QTreeWidgetItem(m_objectTree, {label, QString()});

        cat->setData(0, kCategoryRole, id);

        cat->setFlags(Qt::ItemIsEnabled);

        QFont font = cat->font(0);

        font.setBold(true);

        cat->setFont(0, font);

        cat->setExpanded(expanded.contains(categoryExpansionKey(id)) || count > 0);

        return cat;

    };



    auto addEditableItem = [&](QTreeWidgetItem* parent, const QString& name, const QString& kind,

                               GraphObjectKind kindEnum, void* ptr,

                               TwoPortItem* ownerTwoPort = nullptr) -> QTreeWidgetItem* {

        auto* treeItem =

            parent ? new QTreeWidgetItem(parent, {name, kind}) : new QTreeWidgetItem(m_objectTree, {name, kind});

        setItemObject(treeItem, kindEnum, ptr);

        treeItem->setIcon(0, objectListIcon(kindEnum));

        treeItem->setData(1, kKindLabelRole, kind);

        if (ownerTwoPort) {

            setItemParentTwoPort(treeItem, ownerTwoPort);

        }

        treeItem->setFlags(treeItem->flags() | Qt::ItemIsEditable);

        return treeItem;

    };



    if (!twoPorts.isEmpty()) {

        QTreeWidgetItem* category = addCategory(QStringLiteral("twoport"), tr("Two-Ports"), twoPorts.size());

        for (TwoPortItem* twoPort : twoPorts) {

            QTreeWidgetItem* root = addEditableItem(category, twoPort->name(), twoPortKindLabel(twoPort->kind()),

                                                    GraphObjectKind::TwoPort, twoPort);

            root->setExpanded(expanded.contains(objectExpansionKey(GraphObjectKind::TwoPort, twoPort)));

            addEditableItem(root, twoPort->v1()->name(), QStringLiteral("v1"), GraphObjectKind::Node, twoPort->v1(),

                            twoPort);

            addEditableItem(root, twoPort->v2()->name(), QStringLiteral("v2"), GraphObjectKind::Node, twoPort->v2(),

                            twoPort);

            addEditableItem(root, twoPort->g1()->name(), QStringLiteral("g1"), GraphObjectKind::Node, twoPort->g1(),

                            twoPort);

            if (twoPort->g1() != twoPort->g2()) {

                addEditableItem(root, twoPort->g2()->name(), QStringLiteral("g2"), GraphObjectKind::Node,

                                twoPort->g2(), twoPort);

            }

            addEditableItem(root, twoPort->leftBranch()->name(), tr("left"), GraphObjectKind::Branch,

                            twoPort->leftBranch(), twoPort);

            addEditableItem(root, twoPort->rightBranch()->name(), tr("right"), GraphObjectKind::Branch,

                            twoPort->rightBranch(), twoPort);

        }

    }



    if (!standaloneNodes.isEmpty()) {

        QTreeWidgetItem* category = addCategory(QStringLiteral("node"), tr("Nodes"), standaloneNodes.size());

        for (NodeItem* node : standaloneNodes) {

            addEditableItem(category, node->name(), objectKindLabel(GraphObjectKind::Node), GraphObjectKind::Node,

                            node);

        }

    }



    if (!standaloneBranches.isEmpty()) {

        QTreeWidgetItem* category = addCategory(QStringLiteral("branch"), tr("Branches"), standaloneBranches.size());

        for (BranchItem* branch : standaloneBranches) {

            addEditableItem(category, branch->name(), objectKindLabel(GraphObjectKind::Branch),

                            GraphObjectKind::Branch, branch);

        }

    }



    m_syncingObjectTree = false;

    syncObjectTreeSelection();

}



void MainWindow::onObjectTreeSelectionChanged() {

    if (m_syncingObjectTree) {

        return;

    }

    const QList<QTreeWidgetItem*> items = m_objectTree->selectedItems();

    if (items.isEmpty()) {

        m_blockSceneSelectionSync = true;

        m_scene->clearSelection();

        m_blockSceneSelectionSync = false;

        updatePropertyPanel();

        return;

    }



    m_blockSceneSelectionSync = true;

    m_scene->clearSelection();

    const bool singleSelect = items.size() == 1;

    for (QTreeWidgetItem* item : items) {

        if (!objectPtr(item)) {

            continue;

        }

        switch (objectKind(item)) {

        case GraphObjectKind::Node: {

            auto* node = static_cast<NodeItem*>(objectPtr(item));

            if (TwoPortItem* owner = itemParentTwoPort(item)) {

                if (singleSelect) {

                    m_scene->selectTwoPortNode(node, owner);

                } else {

                    node->setSelected(true);

                }

            } else {

                node->setSelected(true);

            }

            break;

        }

        case GraphObjectKind::Branch: {

            auto* branch = static_cast<BranchItem*>(objectPtr(item));

            branch->setSelected(true);

            break;

        }

        case GraphObjectKind::TwoPort:

            if (singleSelect) {

                m_scene->selectTwoPort(static_cast<TwoPortItem*>(objectPtr(item)));

            } else {

                static_cast<TwoPortItem*>(objectPtr(item))->setSelected(true);

            }

            break;

        }

    }

    m_blockSceneSelectionSync = false;

    updatePropertyPanel();

}



void MainWindow::onPropertyTableItemChanged(QTableWidgetItem* item) {

    if (m_updatingPropertyPanel || item->column() != 1 || !m_propertyTargetPtr) {

        return;

    }

    QTableWidgetItem* propItem = m_propertyTable->item(item->row(), 0);

    if (!propItem) {

        return;

    }

    const QString property = propItem->text();
    if (property != tr("Name") && property != tr("Through variable")) {

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

    if (m_syncingObjectTree) {

        return;

    }

    if (column != 0) {

        item->setText(1, item->data(1, kKindLabelRole).toString());

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

    if (m_syncingObjectTree || m_blockSceneSelectionSync || m_clearingDocument) {

        return;

    }



    const QVector<GraphSelectionEntry> entries = primarySceneSelection(m_scene);



    m_syncingObjectTree = true;

    m_objectTree->clearSelection();

    if (!entries.isEmpty()) {

        QTreeWidgetItem* firstMatch = nullptr;

        for (const GraphSelectionEntry& entry : entries) {

            if (QTreeWidgetItem* treeItem = pickObjectTreeItem(entries, entry, m_objectTree, m_scene)) {

                treeItem->setSelected(true);

                if (!firstMatch) {

                    firstMatch = treeItem;

                }

            }

        }

        if (firstMatch) {

            m_objectTree->setCurrentItem(firstMatch);

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

    if (stateSpace.status == lg::StateSpaceResult::Status::Ok) {

        if (auto* panelAction = findChild<QAction*>(QStringLiteral("view.stateSpacePanel"))) {

            QSignalBlocker blocker(panelAction);

            panelAction->setChecked(true);

        }

        m_stateSpaceDock->show();

    } else {

        if (auto* panelAction = findChild<QAction*>(QStringLiteral("view.stateSpacePanel"))) {

            QSignalBlocker blocker(panelAction);

            panelAction->setChecked(false);

        }

        m_stateSpaceDock->hide();

    }

}



void MainWindow::updatePropertyPanel() {

    if (m_clearingDocument) {

        return;

    }

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



    auto appendNormalTreeAnalysis = [this, &addRow](const lg::NormalTreeResult& normalTree) {

        addRow(tr("Analysis"), tr("Normal tree"));

        if (m_scene->mode() == GraphScene::Mode::SelectNormalTree) {
            addRow(tr("Source"), tr("Manual selection (editing)"));
        } else if (m_scene->normalTreeIsManual()) {
            addRow(tr("Source"), tr("Manual selection"));
        } else {
            addRow(tr("Source"), tr("Auto-detected"));
        }

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



    QVector<GraphSelectionEntry> entries = primarySceneSelection(m_scene);

    if (entries.isEmpty()) {

        for (QTreeWidgetItem* item : m_objectTree->selectedItems()) {

            if (objectPtr(item)) {

                entries.push_back({objectPtr(item), objectKind(item)});

            }

        }

    }



    void* ptr = nullptr;

    GraphObjectKind kind = GraphObjectKind::Node;



    if (m_scene->normalTreeHighlightActive()) {

        appendNormalTreeAnalysis(m_scene->lastNormalTreeResult());

    }

    if (m_scene->mode() == GraphScene::Mode::SelectNormalTree) {
        int inTreeCount = 0;
        for (QGraphicsItem* item : m_scene->items()) {
            if (auto* branch = dynamic_cast<BranchItem*>(item)) {
                if (branch->normalTreeRoleKnown() && branch->inNormalTree()) {
                    ++inTreeCount;
                }
            }
        }
        const lg::NormalTreeResult& validation = m_scene->manualNormalTreeValidation();
        const bool valid = validation.status == lg::NormalTreeResult::Status::Ok;
        addRow(tr("Selection mode"), tr("Editing normal tree"));
        addRow(tr("Branches in tree"), QString::number(inTreeCount));
        addRow(tr("Validation"),
               valid ? tr("Valid")
                     : (validation.message.isEmpty() ? tr("Invalid") : validation.message));

        const int actionsRow = addLabelRow(tr("Actions"));
        auto* actions = new QWidget(m_propertyTable);
        auto* actionsLayout = new QHBoxLayout(actions);
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        auto* applyBtn = new QPushButton(tr("Apply"), actions);
        auto* cancelBtn = new QPushButton(tr("Cancel"), actions);
        applyBtn->setEnabled(valid);
        actionsLayout->addWidget(applyBtn);
        actionsLayout->addWidget(cancelBtn);
        connect(applyBtn, &QPushButton::clicked, this, [this]() {
            m_scene->acceptManualNormalTreeSelection();
            m_view->setToolMode(GraphScene::Mode::Select);
        });
        connect(cancelBtn, &QPushButton::clicked, this, [this]() {
            m_scene->cancelManualNormalTreeSelection();
            m_view->setToolMode(GraphScene::Mode::Select);
            statusBar()->showMessage(tr("Manual normal tree selection cancelled."), 3000);
            updatePropertyPanel();
        });
        m_propertyTable->setCellWidget(actionsRow, 1, actions);
        addRow(tr("Hint"), tr("Click branches to toggle; Enter to apply, Esc to cancel"));
    }



    if (entries.size() > 1) {

        addRow(tr("Selection"), tr("%1 objects selected").arg(entries.size()));

        QStringList names;

        names.reserve(entries.size());

        for (const GraphSelectionEntry& entry : entries) {

            names.push_back(objectLabel(entry.kind, entry.ptr));

        }

        addRow(tr("Objects"), names.join(QStringLiteral(", ")));

        m_updatingPropertyPanel = false;

        updateFlipBranchAction();

        return;

    }



    if (entries.size() == 1) {

        ptr = entries.front().ptr;

        kind = entries.front().kind;

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

            if (m_scene->twoPortForNode(node)) {

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

            if (TwoPortItem* parentTwoPort = m_scene->twoPortForNode(node)) {

                addRow(tr("Parent"), parentTwoPort->name());

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

            addRow(tr("Through variable"), lg::branchThroughSymbol(*branch), true);

            addRow(tr("Across variable"), lg::branchAcrossVariableText(*branch));

            if (branch->isActive()) {

                addRow(tr("Input"), lg::branchSourceInputDisplay(*branch));

            }

            if (TwoPortItem* twoPort = m_scene->twoPortFor(branch);
                twoPort && GraphScene::isInternalTwoPortBranch(twoPort, branch)) {
                addRow(tr("Parent"), twoPort->name());
                addRow(tr("Through variable"), lg::branchThroughSymbol(*branch));
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

                if (m_scene->mode() == GraphScene::Mode::SelectNormalTree) {
                    const int treeRoleRow = addLabelRow(tr("Normal tree"));
                    auto* treeRoleCombo = new QComboBox(m_propertyTable);
                    treeRoleCombo->addItem(tr("Tree branch"), true);
                    treeRoleCombo->addItem(tr("Link"), false);
                    treeRoleCombo->setCurrentIndex(branch->inNormalTree() ? 0 : 1);
                    if (branch->isActive() && branch->branchType() == BranchType::A) {
                        treeRoleCombo->setEnabled(false);
                    }
                    connect(treeRoleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                            [this, branch, treeRoleCombo](int index) {
                                if (m_updatingPropertyPanel) {
                                    return;
                                }
                                m_scene->setManualNormalTreeBranchRole(
                                    branch, treeRoleCombo->itemData(index).toBool());
                                updatePropertyPanel();
                            });
                    m_propertyTable->setCellWidget(treeRoleRow, 1, treeRoleCombo);
                } else {
                    addRow(tr("Normal tree"), branch->inNormalTree() ? tr("Tree branch") : tr("Link"));
                }

                if (!branch->isActive()) {
                    if (branch->inNormalTree() && branch->branchType() == BranchType::A) {
                        addRow(tr("State variable"),
                               lg::branchAcrossSymbol(*branch));
                    } else if (!branch->inNormalTree() && branch->branchType() == BranchType::T) {
                        addRow(tr("State variable"), lg::branchThroughSymbol(*branch));
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
                constantEdit->setPlaceholderText(tr("e.g. R1, C2, M3"));
                connect(constantEdit, &QLineEdit::editingFinished, this, [this, branch, constantEdit]() {

                    if (m_updatingPropertyPanel) {

                        return;

                    }

                    const QString text = constantEdit->text();
                    if (!lg::isValidElementConstant(text)) {
                        constantEdit->setText(branch->elementConstant());
                        return;
                    }

                    m_scene->pushSetBranchConstant(branch, text);
                    updatePropertyPanel();

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

    } else {

        addRow(tr("Selection"), tr("Nothing selected"));

    }

    m_updatingPropertyPanel = false;

    updateFlipBranchAction();

}



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

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() != QEvent::KeyPress) {
        return QMainWindow::eventFilter(watched, event);
    }
    if (watched != m_view && watched != m_view->viewport()) {
        return QMainWindow::eventFilter(watched, event);
    }

    const auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() != Qt::Key_Escape || key->modifiers() != Qt::NoModifier) {
        return QMainWindow::eventFilter(watched, event);
    }
    if (m_scene->mode() != GraphScene::Mode::Select || m_scene->selectedItems().isEmpty()) {
        return QMainWindow::eventFilter(watched, event);
    }

    m_blockSceneSelectionSync = true;
    m_scene->clearSelection();
    m_objectTree->clearSelection();
    m_blockSceneSelectionSync = false;
    updatePropertyPanel();
    return true;
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

void MainWindow::showAnalyzeWindow() {
    if (!m_analyzeDock) {
        return;
    }
    m_analyzeDock->show();
    m_analyzeDock->raise();
    if (auto* panelAction = findChild<QAction*>(QStringLiteral("view.analyzePanel"))) {
        QSignalBlocker blocker(panelAction);
        panelAction->setChecked(true);
    }
}

void MainWindow::showConsoleWindow() {
    if (!m_consoleDock) {
        return;
    }
    m_consoleDock->show();
    m_consoleDock->raise();
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
    const QString exampleTag = m_isExampleDocument ? tr(" (Example)") : QString();
    setWindowTitle(tr("%1%2 [*] - LGM").arg(name, exampleTag));
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
    m_clearingDocument = true;
    m_scene->clearDocument();
    m_clearingDocument = false;
    m_currentFilePath.clear();
    m_isExampleDocument = false;
    m_scene->undoStack()->setClean();
    updateWindowTitle();
    m_objectTree->clearSelection();
    updateObjectList();
    updatePropertyPanel();
    updateStateSpacePanel();
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
    loadDocumentFromPath(path);
}

void MainWindow::fileOpenExample(const QString& path) {
    if (!confirmDiscardChanges()) {
        return;
    }
    loadDocumentFromPath(path, true);
}

bool MainWindow::loadDocumentFromPath(const QString& path, bool fromExamplesMenu) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open Failed"), tr("Could not read %1.").arg(path));
        return false;
    }

    QString error;
    if (!m_scene->documentFromJson(file.readAll(), &error)) {
        QMessageBox::warning(this, tr("Open Failed"), error);
        return false;
    }

    const QString loadWarning = m_scene->takeLoadWarning();
    if (!loadWarning.isEmpty()) {
        QMessageBox::warning(this, tr("Open"), loadWarning);
    }

    m_currentFilePath = path;
    m_isExampleDocument = fromExamplesMenu || isExampleFilePath(path);
    m_scene->undoStack()->setClean();
    syncDefaultSystemTypeCombo(m_scene->defaultSystemType());
    updateWindowTitle();
    updateObjectList();
    updatePropertyPanel();
    updateStateSpacePanel();
    return true;
}

bool MainWindow::isExampleFilePath(const QString& path) const {
    if (path.isEmpty()) {
        return false;
    }
    const QString root = examplesDir();
    if (root.isEmpty()) {
        return false;
    }
    QString dir = QFileInfo(root).absoluteFilePath();
    if (dir.isEmpty()) {
        return false;
    }
    if (!dir.endsWith(QLatin1Char('/')) && !dir.endsWith(QLatin1Char('\\'))) {
        dir += QDir::separator();
    }
    const QString file = QFileInfo(path).absoluteFilePath();
    if (file.isEmpty()) {
        return false;
    }
    return file.startsWith(dir, Qt::CaseInsensitive);
}

bool MainWindow::fileSave() {
    if (m_currentFilePath.isEmpty() || m_isExampleDocument) {
        return fileSaveAs();
    }
    return writeDocument(m_currentFilePath);
}

bool MainWindow::fileSaveAs() {
    const QString initialPath = m_isExampleDocument ? QString() : m_currentFilePath;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save As"), initialPath,
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
    m_isExampleDocument = false;
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
