#pragma once

#include "canvas.h"
#include "tool_icons.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QIcon>
#include <QKeySequence>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStyle>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVector>

class GraphScene;
class GraphView;

namespace mw {

constexpr int kConsoleTab = 0;
constexpr int kStateSpaceTab = 1;

enum class GraphObjectKind { Node, Branch, TwoPort };

constexpr int kObjectPtrRole = Qt::UserRole;
constexpr int kObjectKindRole = Qt::UserRole + 1;
constexpr int kCategoryRole = Qt::UserRole + 2;
constexpr int kKindLabelRole = Qt::UserRole + 3;
constexpr int kParentTwoPortRole = Qt::UserRole + 4;

struct GraphSelectionEntry {
    void* ptr = nullptr;
    GraphObjectKind kind = GraphObjectKind::Node;
};

void setItemObject(QTreeWidgetItem* item, GraphObjectKind kind, void* ptr);
void setItemParentTwoPort(QTreeWidgetItem* item, TwoPortItem* twoPort);
TwoPortItem* itemParentTwoPort(const QTreeWidgetItem* item);
void* objectPtr(const QTreeWidgetItem* item);
GraphObjectKind objectKind(const QTreeWidgetItem* item);
QString objectExpansionKey(GraphObjectKind kind, void* ptr);
QString categoryExpansionKey(const QString& id);
QIcon objectListIcon(GraphObjectKind kind);
QString twoPortKindLabel(TwoPortKind kind);
QString objectKindLabel(GraphObjectKind kind);
QString examplesDir();
void populateSystemTypeCombo(QComboBox* combo);
QString objectLabel(GraphObjectKind kind, void* ptr);
QVector<GraphSelectionEntry> primarySceneSelection(const GraphScene* scene);
BranchItem* singleSelectedBranch(const GraphScene* scene);
QTreeWidgetItem* pickObjectTreeItem(const QVector<GraphSelectionEntry>& entries,
                                    const GraphSelectionEntry& entry, QTreeWidget* tree,
                                    const GraphScene* scene);
QIcon themedIcon(const char* name, QStyle::StandardPixmap fallback);
QAction* addTool(QActionGroup* group, QToolBar* toolbar, const QIcon& icon, const QString& text,
                 const char* shortcutId, GraphScene::Mode mode, GraphView* view);
QString shortcutToolTip(const QString& label, const QKeySequence& shortcut);

}  // namespace mw
