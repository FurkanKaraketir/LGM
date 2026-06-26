#pragma once

#include "state_space.h"

#include <functional>

#include <QWidget>

class GraphScene;
class GraphView;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;

namespace lg {

void populateStateSpaceLayout(QVBoxLayout* layout, QWidget* parent, const StateSpaceResult& stateSpace);

}  // namespace lg

class AnalyzeWindow : public QWidget {
    Q_OBJECT

public:
    explicit AnalyzeWindow(GraphScene* scene, GraphView* view, QWidget* parent = nullptr);

    void setRefreshCallback(std::function<void()> callback) { m_refreshCallback = std::move(callback); }

signals:
    void stateSpaceComputed();

private:
    void refreshNormalTreeSection();
    void refreshValidTreesList();
    void refreshSavedTreesList();
    void runFindNormalTree();
    void runSelectNormalTree();
    void runClearNormalTree();
    void runUseValidTree();
    void runSaveCurrentTree();
    void runRemoveSavedTree();
    void runUseSavedTree();
    void runComputeStateSpace();

    GraphScene* m_scene = nullptr;
    GraphView* m_view = nullptr;
    std::function<void()> m_refreshCallback;

    QLabel* m_normalTreeStatus = nullptr;
    QWidget* m_manualTreePanel = nullptr;
    QLabel* m_manualTreeStatus = nullptr;
    QPushButton* m_manualApplyBtn = nullptr;
    QListWidget* m_validTreesList = nullptr;
    QPushButton* m_useValidTreeBtn = nullptr;
    QListWidget* m_savedTreesList = nullptr;
    QPushButton* m_saveTreeBtn = nullptr;
    QPushButton* m_removeTreeBtn = nullptr;
    QPushButton* m_useTreeBtn = nullptr;
};
