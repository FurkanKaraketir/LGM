#include "canvas.h"

#include <QSet>
#include <algorithm>
#include <vector>

void GraphScene::captureDeleteState(std::vector<NodeSnapshot>& nodes, std::vector<BranchSnapshot>& branches,
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
        twoPorts.push_back(
            {snap(twoPort->center()), twoPort->kind(), twoPort->modulus(), twoPort->name()});
    }

    for (NodeItem* node : nodesToDelete) {
        if (node->twoPort()) {
            continue;
        }
        NodeSnapshot snap;
        snap.pos = node->scenePos();
        snap.name = node->name();
        snap.ground = node->isGround();
        snap.across = node->isGround() ? QStringLiteral("0") : node->acrossVariable();
        snap.systemType = node->systemType();
        nodes.push_back(snap);
    }

    for (BranchItem* branch : branchesToDelete) {
        if (TwoPortItem* twoPort = twoPortFor(branch)) {
            if (isInternalTwoPortBranch(twoPort, branch)) {
                continue;
            }
        }
        BranchSnapshot snap;
        snap.from = branch->from()->scenePos();
        snap.to = branch->to()->scenePos();
        snap.index = branch->index();
        snap.name = branch->name();
        snap.active = branch->isActive();
        snap.type = branch->branchType();
        snap.constant = branch->elementConstant();
        snap.bow = branch->bow();
        snap.serialId = branch->serialId();
        snap.sourceInputId = branch->sourceInputId();
        branches.push_back(snap);
    }
}

void GraphScene::executeDelete(const std::vector<NodeSnapshot>& nodes,
                               const std::vector<BranchSnapshot>& branches,
                               const std::vector<TwoPortKey>& twoPorts) {
    clearBranchPending();
    clearSelection();

    for (const TwoPortKey& key : twoPorts) {
        if (TwoPortItem* item = twoPortAtCenter(key.center)) {
            purgeTwoPort(item);
        }
    }

    const auto deletingNode = [&](const QPointF& pos) {
        return std::any_of(nodes.begin(), nodes.end(),
                           [&](const NodeSnapshot& snap) { return snap.pos == pos; });
    };

    std::vector<BranchSnapshot> loneBranches;
    for (const BranchSnapshot& snap : branches) {
        if (deletingNode(snap.from) || deletingNode(snap.to)) {
            continue;
        }
        loneBranches.push_back(snap);
    }
    std::sort(loneBranches.begin(), loneBranches.end(),
              [](const BranchSnapshot& a, const BranchSnapshot& b) { return a.index > b.index; });

    for (const BranchSnapshot& snap : loneBranches) {
        NodeItem* a = nodeAtPos(snap.from);
        NodeItem* b = nodeAtPos(snap.to);
        if (!a || !b) {
            continue;
        }
        for (BranchItem* branch : branchesBetween(a, b)) {
            if (branch->index() == snap.index) {
                destroyBranch(branch);
                break;
            }
        }
    }

    for (const NodeSnapshot& snap : nodes) {
        if (NodeItem* node = nodeAtPos(snap.pos)) {
            destroyNode(node);
        }
    }
}

void GraphScene::restoreDelete(const std::vector<NodeSnapshot>& nodes,
                               const std::vector<BranchSnapshot>& branches,
                               const std::vector<TwoPortKey>& twoPorts) {
    for (const TwoPortKey& key : twoPorts) {
        createTwoPort(key.center, key.kind, key.modulus, key.name);
    }

    for (const NodeSnapshot& snap : nodes) {
        if (NodeItem* node = createNode(snap.pos)) {
            node->setName(snap.name);
            node->setGround(snap.ground);
            if (!snap.ground) {
                node->setAcrossVariable(snap.across);
                node->setSystemType(snap.systemType);
            }
        }
    }

    std::vector<BranchSnapshot> sortedBranches = branches;
    std::sort(sortedBranches.begin(), sortedBranches.end(),
              [](const BranchSnapshot& a, const BranchSnapshot& b) { return a.index < b.index; });

    for (const BranchSnapshot& snap : sortedBranches) {
        NodeItem* a = nodeAtPos(snap.from);
        NodeItem* b = nodeAtPos(snap.to);
        if (!a || !b) {
            continue;
        }
        if (BranchItem* branch = createBranch(a, b, snap.bow)) {
            if (snap.serialId > 0) {
                branch->setSerialId(snap.serialId);
                if (snap.serialId >= m_nextBranchId) {
                    m_nextBranchId = snap.serialId + 1;
                }
            }
            branch->setName(snap.name);
            branch->setBranchType(snap.type);
            if (snap.sourceInputId > 0) {
                branch->setSourceInputId(snap.sourceInputId);
                registerSourceInputId(snap.sourceInputId);
            }
            branch->setActive(snap.active);
            if (!snap.active) {
                branch->setElementConstant(snap.constant);
            }
        }
    }
}
