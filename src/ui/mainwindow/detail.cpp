#include "detail.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QGraphicsItem>
#include <QTreeWidgetItemIterator>

#ifdef EXAMPLES_SOURCE_DIR
#endif

namespace mw {

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
#ifdef Q_OS_MACOS
    const QString inResources = QDir(QCoreApplication::applicationDirPath())
                                    .absoluteFilePath(QStringLiteral("../Resources/Examples"));
    if (QDir(inResources).exists()) {
        return inResources;
    }
#endif
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

}  // namespace mw
