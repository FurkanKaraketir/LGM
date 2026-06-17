#pragma once

#include "canvas.h"

#include <QMainWindow>

class QAction;
class QActionGroup;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildToolbar();
    void buildStatusBar();
    void syncModeUi(GraphScene::Mode mode);
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
};
