#include "canvas.h"

#include <QSet>
#include <algorithm>
#include <vector>

void GraphScene::captureDeleteState(std::vector<QPointF>& nodes, std::vector<BranchKey>& branches,
                                    std::vector<TwoPortKey>& twoPorts) const {
    nodes.clear();
    branches.clear();
    twoPorts.clear();

    QSet<TwoPortItem*> twoPortsToDelete;
    QSet<NodeItem*> nodesToDelete;
    QSet<BranchItem*> branchesToDelete;

    for (QGraphicsItem* item : selectedItems()) {
        if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            twoPortsToDelete.insert(twoPort);
        } else if (auto* node = dynamic_cast<NodeItem*>(item)) {
            if (TwoPortItem* twoPort = node->twoPort()) {
                twoPortsToDelete.insert(twoPort);
            } else {
                nodesToDelete.insert(node);
            }
        } else if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            if (TwoPortItem* twoPort = twoPortFor(branch)) {
                if (isInternalTwoPortBranch(twoPort, branch)) {
                    twoPortsToDelete.insert(twoPort);
                } else {
                    branchesToDelete.insert(branch);
                }
            } else {
                branchesToDelete.insert(branch);
            }
        }
    }

    for (NodeItem* node : nodesToDelete) {
        for (BranchItem* branch : node->branches()) {
            branchesToDelete.insert(branch);
        }
    }

    for (TwoPortItem* twoPort : twoPortsToDelete) {
        twoPorts.push_back({snap(twoPort->center()), twoPort->kind()});
    }

    for (NodeItem* node : nodesToDelete) {
        if (node->twoPort()) {
            continue;
        }
        nodes.push_back(node->scenePos());
    }

    for (BranchItem* branch : branchesToDelete) {
        if (TwoPortItem* twoPort = twoPortFor(branch)) {
            if (isInternalTwoPortBranch(twoPort, branch)) {
                continue;
            }
        }
        branches.push_back({branch->from()->scenePos(), branch->to()->scenePos(), branch->index()});
    }
}

void GraphScene::executeDelete(const std::vector<QPointF>& nodes, const std::vector<BranchKey>& branches,
                               const std::vector<TwoPortKey>& twoPorts) {
    clearBranchPending();
    clearSelection();

    for (const TwoPortKey& key : twoPorts) {
        if (TwoPortItem* item = twoPortAtCenter(key.center)) {
            destroyTwoPort(item);
        }
    }

    const auto deletingNode = [&](const QPointF& pos) {
        return std::find(nodes.begin(), nodes.end(), pos) != nodes.end();
    };

    std::vector<BranchKey> loneBranches;
    for (const BranchKey& key : branches) {
        if (deletingNode(key.from) || deletingNode(key.to)) {
            continue;
        }
        loneBranches.push_back(key);
    }
    std::sort(loneBranches.begin(), loneBranches.end(),
              [](const BranchKey& a, const BranchKey& b) { return a.index > b.index; });

    for (const BranchKey& key : loneBranches) {
        NodeItem* a = nodeAtPos(key.from);
        NodeItem* b = nodeAtPos(key.to);
        if (!a || !b) {
            continue;
        }
        for (BranchItem* branch : branchesBetween(a, b)) {
            if (branch->index() == key.index) {
                destroyBranch(branch);
                break;
            }
        }
    }

    for (const QPointF& pos : nodes) {
        if (NodeItem* node = nodeAtPos(pos)) {
            destroyNode(node);
        }
    }
}

void GraphScene::restoreDelete(const std::vector<QPointF>& nodes, const std::vector<BranchKey>& branches,
                               const std::vector<TwoPortKey>& twoPorts) {
    for (const TwoPortKey& key : twoPorts) {
        createTwoPort(key.center, key.kind);
    }

    for (const QPointF& pos : nodes) {
        createNode(pos);
    }

    for (const BranchKey& key : branches) {
        NodeItem* a = nodeAtPos(key.from);
        NodeItem* b = nodeAtPos(key.to);
        if (a && b) {
            createBranch(a, b);
        }
    }
}
