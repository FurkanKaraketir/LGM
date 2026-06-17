#pragma once

#include "canvas.h"

#include <QMainWindow>

class QAction;
class QActionGroup;
class QDockWidget;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QTableWidget;
class QTableWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildMenuBar();
    void buildToolbar();
    void buildStatusBar();
    void buildDockPanels();
    void syncModeUi(GraphScene::Mode mode);
    void updateObjectList();
    void updatePropertyPanel();
    void syncObjectTreeSelection();
    void onObjectTreeSelectionChanged();
    void onObjectTreeItemChanged(QTreeWidgetItem* item, int column);
    void onPropertyTableItemChanged(QTableWidgetItem* item);
    void updateFlipBranchAction();
    static QString modeStatusText(GraphScene::Mode mode);

    GraphScene* m_scene = nullptr;
    GraphView* m_view = nullptr;
    QActionGroup* m_modeGroup = nullptr;
    QAction* m_selectAction = nullptr;
    QAction* m_addNodeAction = nullptr;
    QAction* m_addBranchAction = nullptr;
    QAction* m_addTwoPortAction = nullptr;
    QAction* m_deleteAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_flipBranchAction = nullptr;
    QAction* m_mergeNodesAction = nullptr;
    
    QDockWidget* m_propertyDock = nullptr;
    QDockWidget* m_objectListDock = nullptr;
    QTableWidget* m_propertyTable = nullptr;
    QTreeWidget* m_objectTree = nullptr;
    bool m_syncingObjectTree = false;
    bool m_blockSceneSelectionSync = false;
    bool m_updatingPropertyPanel = false;
    void* m_propertyTargetPtr = nullptr;
    int m_propertyTargetKind = -1;
};
