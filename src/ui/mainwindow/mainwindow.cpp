#include "mainwindow.h"
#include "detail.h"
#include "common_includes.h"

using namespace mw;

#include "analyze_window.h"
#include "app_settings.h"
#include "app_shortcuts.h"
#include "canvas.h"
#include "settings_window.h"
#include "tool_icons.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleHints>
#include <QToolBar>
#include <QUndoStack>

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

    // Side docks span full height; bottom output sits under the canvas only.
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

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
