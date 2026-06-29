#include "mainwindow.h"
#include "detail.h"

using namespace mw;

#include "analyze_window.h"
#include "app_log.h"
#include "canvas.h"
#include "normal_tree.h"
#include "state_space.h"

#include <QAbstractItemView>
#include <QDockWidget>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

void MainWindow::buildStatusBar() {

    auto* hint = new QLabel(modeStatusText(GraphScene::Mode::Select), this);

    hint->setContentsMargins(8, 0, 8, 0);

    statusBar()->addPermanentWidget(hint, 1);



    connect(m_scene, &GraphScene::modeChanged, this, [hint](GraphScene::Mode mode) {

        hint->setText(modeStatusText(mode));

    });

}



void MainWindow::buildDockPanels() {

    m_sidePanelDock = new QDockWidget(tr("Panels"), this);
    m_sidePanelDock->setObjectName(QStringLiteral("sidePanelDock"));
    m_sidePanelDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_sidePanelDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    m_sidePanelTabs = new QTabWidget(m_sidePanelDock);
    m_sidePanelTabs->setObjectName(QStringLiteral("sidePanelTabs"));
    m_sidePanelTabs->setDocumentMode(true);
    m_sidePanelTabs->setTabsClosable(true);
    m_sidePanelTabs->setMovable(true);

    auto* propertyPanel = new QWidget(m_sidePanelTabs);
    auto* propertyLayout = new QVBoxLayout(propertyPanel);
    propertyLayout->setContentsMargins(0, 0, 0, 0);

    m_propertyTable = new QTableWidget(0, 2, propertyPanel);
    m_propertyTable->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    m_propertyTable->horizontalHeader()->setStretchLastSection(true);
    m_propertyTable->verticalHeader()->setVisible(false);
    m_propertyTable->setAlternatingRowColors(true);
    m_propertyTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    propertyLayout->addWidget(m_propertyTable);

    m_sidePanelTabs->addTab(propertyPanel, tr("Properties"));
    m_sidePanelTabs->tabBar()->setTabData(kPropertiesTab, static_cast<int>(PanelTab::Properties));

    m_analyzePanel = new AnalyzeWindow(m_scene, m_view, m_sidePanelTabs);
    m_analyzePanel->setRefreshCallback([this]() { updatePropertyPanel(); });
    connect(m_analyzePanel, &AnalyzeWindow::stateSpaceComputed, this, &MainWindow::updateStateSpacePanel);
    m_sidePanelTabs->addTab(m_analyzePanel, tr("Analyze"));
    m_sidePanelTabs->tabBar()->setTabData(kAnalyzeTab, static_cast<int>(PanelTab::Analyze));
    m_sidePanelTabs->tabBar()->setTabVisible(kAnalyzeTab, false);

    m_sidePanelDock->setWidget(m_sidePanelTabs);
    addDockWidget(Qt::RightDockWidgetArea, m_sidePanelDock);

    connect(m_sidePanelTabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        const auto tab = static_cast<PanelTab>(m_sidePanelTabs->tabBar()->tabData(index).toInt());
        hidePanelTab(tab);
    });
    connect(m_sidePanelTabs, &QTabWidget::currentChanged, this, &MainWindow::syncPanelMenuActions);



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



    m_outputDock = new QDockWidget(tr("Output"), this);
    m_outputDock->setObjectName(QStringLiteral("outputDock"));
    m_outputDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    m_outputDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    m_outputDock->setMinimumHeight(120);

    m_outputTabs = new QTabWidget(m_outputDock);
    m_outputTabs->setObjectName(QStringLiteral("outputTabs"));
    m_outputTabs->setDocumentMode(true);
    m_outputTabs->setTabsClosable(true);
    m_outputTabs->setMovable(true);

    auto* consolePanel = new QWidget(m_outputTabs);
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

    m_outputTabs->addTab(consolePanel, tr("Console"));
    m_outputTabs->tabBar()->setTabData(kConsoleTab, static_cast<int>(PanelTab::Console));
    m_outputTabs->tabBar()->setTabVisible(kConsoleTab, false);

    auto* stateSpaceScroll = new QScrollArea(m_outputTabs);

    stateSpaceScroll->setWidgetResizable(false);

    stateSpaceScroll->setFrameShape(QFrame::NoFrame);

    stateSpaceScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    stateSpaceScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_stateSpaceScrollContent = new QWidget(stateSpaceScroll);

    m_stateSpaceScrollContent->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    m_stateSpaceLayout = new QVBoxLayout(m_stateSpaceScrollContent);

    m_stateSpaceLayout->setContentsMargins(8, 8, 8, 8);

    m_stateSpaceLayout->setSpacing(4);

    stateSpaceScroll->setWidget(m_stateSpaceScrollContent);

    m_outputTabs->addTab(stateSpaceScroll, tr("State Space"));
    m_outputTabs->tabBar()->setTabData(kStateSpaceTab, static_cast<int>(PanelTab::StateSpace));
    m_outputTabs->tabBar()->setTabVisible(kStateSpaceTab, false);
    m_outputDock->setWidget(m_outputTabs);
    addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);
    m_outputDock->hide();

    connect(m_outputTabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        const auto tab = static_cast<PanelTab>(m_outputTabs->tabBar()->tabData(index).toInt());
        hidePanelTab(tab);
    });
    connect(m_outputTabs, &QTabWidget::currentChanged, this, &MainWindow::syncPanelMenuActions);



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

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::raisePropertyPanelForSelection);

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

    syncPanelMenuActions();

}
