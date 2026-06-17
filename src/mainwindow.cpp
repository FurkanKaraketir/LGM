#include "mainwindow.h"

#include "canvas.h"

#include <QActionGroup>
#include <QApplication>
#include <QLabel>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>

namespace {

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

    buildToolbar();
    buildStatusBar();

    connect(m_scene, &GraphScene::modeChanged, this, &MainWindow::syncModeUi);

    setWindowTitle(tr("Linear Graph Modeling"));
    resize(1000, 720);
}

void MainWindow::buildToolbar() {
    auto* toolbar = addToolBar(tr("Tools"));
    toolbar->setObjectName("mainToolBar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize({22, 22});
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

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
    homeAction->setToolTip(tr("Center view on the graph"));
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

    m_undoAction = toolbar->addAction(themedIcon("edit-undo", QStyle::SP_ArrowBack), tr("Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_undoAction, &QAction::triggered, m_scene, &GraphScene::undo);

    m_redoAction = toolbar->addAction(themedIcon("edit-redo", QStyle::SP_ArrowForward), tr("Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_redoAction, &QAction::triggered, m_scene, &GraphScene::redo);

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
        return tr("Select — drag to move; double-click two-port or press T to switch transformer/gyrator");
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
