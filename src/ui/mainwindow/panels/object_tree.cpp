#include "mainwindow.h"
#include "detail.h"
#include "common_includes.h"

using namespace mw;

#include "canvas.h"

#include <QGraphicsItem>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <algorithm>

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
