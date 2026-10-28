#include "canvas.h"

#include <QSet>
#include <algorithm>
#include <vector>

GraphScene::NodeSnapshot GraphScene::snapshotOf(const NodeItem* node) {
    NodeSnapshot snap;
    snap.id = node->serialId();
    snap.pos = node->scenePos();
    snap.name = node->name();
    snap.ground = node->isGround();
    snap.across = node->isGround() ? QStringLiteral("0") : node->acrossVariable();
    snap.systemType = node->systemType();
    return snap;
}

GraphScene::BranchSnapshot GraphScene::snapshotOf(const BranchItem* branch) {
    BranchSnapshot snap;
    snap.fromId = branch->from()->serialId();
    snap.toId = branch->to()->serialId();
    snap.index = branch->index();
    snap.name = branch->name();
    snap.active = branch->isActive();
    snap.type = branch->branchType();
    snap.constant = branch->elementConstant();
    snap.bow = branch->bow();
    snap.serialId = branch->serialId();
    snap.sourceInputId = branch->sourceInputId();
    return snap;
}

GraphScene::TwoPortSnapshot GraphScene::snapshotOf(const TwoPortItem* twoPort) {
    TwoPortSnapshot snap;
    snap.key = {twoPort->serialId(), twoPort->kind(), twoPort->modulus(), twoPort->name()};
    snap.v1 = snapshotOf(twoPort->v1());
    snap.v2 = snapshotOf(twoPort->v2());
    snap.g1 = snapshotOf(twoPort->g1());
    snap.g2 = snapshotOf(twoPort->g2());
    snap.leftBranch = snapshotOf(twoPort->leftBranch());
    snap.rightBranch = snapshotOf(twoPort->rightBranch());
    return snap;
}

NodeItem* GraphScene::createNodeFromSnapshot(const NodeSnapshot& snap) {
    NodeItem* node = createNode(snap.pos);
    if (!node) {
        return nullptr;
    }
    if (snap.id > 0) {
        node->setSerialId(snap.id);
        m_nextNodeId = std::max(m_nextNodeId, snap.id + 1);
    }
    node->setGround(snap.ground);
    // Domain first: it only rewrites auto-generated across names, then the saved name wins.
    node->setSystemType(snap.systemType);
    if (!snap.ground) {
        node->setAcrossVariable(snap.across);
    }
    node->setName(snap.name);
    return node;
}

void GraphScene::applyBranchSnapshot(BranchItem* branch, const BranchSnapshot& snap) {
    if (!branch) {
        return;
    }
    if (!branch->isTwoPortPort()) {
        branch->setBow(snap.bow);  // port bows are derived from the layout
    }
    if (snap.serialId > 0) {
        branch->setSerialId(snap.serialId);
        m_nextBranchId = std::max(m_nextBranchId, snap.serialId + 1);
    }
    // Before setActive(): an active branch without an id would otherwise allocate a fresh source input.
    if (snap.sourceInputId > 0) {
        branch->setSourceInputId(snap.sourceInputId);
        registerSourceInputId(snap.sourceInputId);
    }
    branch->setActive(snap.active);
    if (!snap.active) {
        branch->setElementConstant(snap.constant);
    }
    branch->setBranchType(snap.type);
    branch->setName(snap.name);
    // Port branches are created v → ref; restore one that was flipped to ref → v.
    if (branch->isTwoPortPort() && snap.fromId > 0 && snap.fromId == branch->to()->serialId()) {
        flipBranch(branch);
    }
}

BranchItem* GraphScene::createBranchFromSnapshot(const BranchSnapshot& snap) {
    NodeItem* a = nodeById(snap.fromId);
    NodeItem* b = nodeById(snap.toId);
    if (!a || !b) {
        return nullptr;
    }
    BranchItem* branch = createBranch(a, b, snap.bow);
    applyBranchSnapshot(branch, snap);
    return branch;
}

TwoPortItem* GraphScene::restoreTwoPortSnapshot(const TwoPortSnapshot& data) {
    // Ports still present (shared with a cascaded sibling, or g1 == g2) are reused by id.
    const auto nodeFor = [&](const NodeSnapshot& snap) -> NodeItem* {
        if (NodeItem* existing = nodeById(snap.id)) {
            return existing;
        }
        return createNodeFromSnapshot(snap);
    };
    NodeItem* v1 = nodeFor(data.v1);
    NodeItem* v2 = nodeFor(data.v2);
    NodeItem* g1 = nodeFor(data.g1);
    NodeItem* g2 = nodeFor(data.g2);
    if (!v1 || !v2 || !g1 || !g2) {
        return nullptr;
    }
    TwoPortItem* twoPort =
        createTwoPortFromPorts(v1, v2, g1, g2, data.key.kind, data.key.modulus, data.key.name);
    if (!twoPort) {
        return nullptr;
    }
    if (data.key.id > 0) {
        twoPort->setSerialId(data.key.id);
        m_nextTwoPortId = std::max(m_nextTwoPortId, data.key.id + 1);
    }
    applyBranchSnapshot(twoPort->leftBranch(), data.leftBranch);
    applyBranchSnapshot(twoPort->rightBranch(), data.rightBranch);
    twoPort->refresh();
    return twoPort;
}

void GraphScene::captureDeleteState(std::vector<NodeSnapshot>& nodes, std::vector<BranchSnapshot>& branches,
                                    std::vector<TwoPortSnapshot>& twoPorts) const {
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
            if (branch->isTwoPortPort()) {
                if (TwoPortItem* twoPort = twoPortFor(branch)) {
                    twoPortsToDelete.insert(twoPort);
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

    // A port node goes with its two-port unless a surviving sibling still owns it; every external
    // branch on such a node is destroyed too, so it must be in the snapshot for undo.
    for (TwoPortItem* twoPort : twoPortsToDelete) {
        for (NodeItem* port : {twoPort->v1(), twoPort->v2(), twoPort->g1(), twoPort->g2()}) {
            const bool survives =
                std::any_of(port->twoPorts().begin(), port->twoPorts().end(),
                            [&](TwoPortItem* owner) { return !twoPortsToDelete.contains(owner); });
            if (survives) {
                continue;
            }
            for (BranchItem* branch : port->branches()) {
                if (!branch->isTwoPortPort()) {
                    branchesToDelete.insert(branch);
                }
            }
        }
    }

    // Two-ports are restored in this order; deterministic ids keep cascades re-linking the same way.
    std::vector<TwoPortItem*> orderedTwoPorts(twoPortsToDelete.begin(), twoPortsToDelete.end());
    std::sort(orderedTwoPorts.begin(), orderedTwoPorts.end(),
              [](const TwoPortItem* a, const TwoPortItem* b) { return a->serialId() < b->serialId(); });
    for (TwoPortItem* twoPort : orderedTwoPorts) {
        twoPorts.push_back(snapshotOf(twoPort));
    }

    for (NodeItem* node : nodesToDelete) {
        nodes.push_back(snapshotOf(node));
    }

    for (BranchItem* branch : branchesToDelete) {
        if (branch->isTwoPortPort()) {
            continue;
        }
        branches.push_back(snapshotOf(branch));
    }
}

void GraphScene::executeDelete(const std::vector<NodeSnapshot>& nodes,
                               const std::vector<BranchSnapshot>& branches,
                               const std::vector<TwoPortSnapshot>& twoPorts) {
    clearBranchPending();
    clearSelection();

    for (const TwoPortSnapshot& snap : twoPorts) {
        if (TwoPortItem* item = twoPortById(snap.key.id)) {
            purgeTwoPort(item);
        }
    }

    for (const BranchSnapshot& snap : branches) {
        BranchItem* branch = branchBySerialId(snap.serialId);
        if (branch && !branch->isTwoPortPort()) {
            destroyBranch(branch);
        }
    }

    for (const NodeSnapshot& snap : nodes) {
        if (NodeItem* node = nodeById(snap.id)) {
            destroyNode(node);
        }
    }
}

void GraphScene::restoreDelete(const std::vector<NodeSnapshot>& nodes,
                               const std::vector<BranchSnapshot>& branches,
                               const std::vector<TwoPortSnapshot>& twoPorts) {
    m_suppressGraphChange = true;
    for (const TwoPortSnapshot& snap : twoPorts) {
        restoreTwoPortSnapshot(snap);
    }

    for (const NodeSnapshot& snap : nodes) {
        createNodeFromSnapshot(snap);
    }

    std::vector<BranchSnapshot> sortedBranches = branches;
    std::sort(sortedBranches.begin(), sortedBranches.end(),
              [](const BranchSnapshot& a, const BranchSnapshot& b) { return a.index < b.index; });

    for (const BranchSnapshot& snap : sortedBranches) {
        createBranchFromSnapshot(snap);
    }
    m_suppressGraphChange = false;
    notifyGraphChanged();
}
