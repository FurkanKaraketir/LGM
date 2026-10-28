#include "canvas.h"
#include "scene_detail.h"

#include <algorithm>

namespace {

enum class PortClass { None, Across, Reference };

// Merges only ever join like with like, so a junction shared by cascaded two-ports has one class.
PortClass portClass(const NodeItem* node) {
    for (const TwoPortItem* twoPort : node->twoPorts()) {
        const int roles = twoPort->portRoles(node);
        if (roles & 3) {
            return PortClass::Across;
        }
        if (roles & 12) {
            return PortClass::Reference;
        }
    }
    return PortClass::None;
}

bool isReferenceOf(const TwoPortItem* twoPort, const NodeItem* node) {
    return node == twoPort->g1() || node == twoPort->g2();
}

}  // namespace

bool GraphScene::canMergeNodes(const NodeItem* a, const NodeItem* b, QString* reason) const {
    const auto fail = [&](const QString& text) {
        if (reason) {
            *reason = text;
        }
        return false;
    };
    if (!a || !b || a == b) {
        return fail(tr("Select two distinct nodes."));
    }
    for (BranchItem* branch :
         branchesBetween(const_cast<NodeItem*>(a), const_cast<NodeItem*>(b))) {
        if (branch->isTwoPortPort()) {
            return fail(tr("These nodes are joined by a two-port port branch and cannot be combined."));
        }
    }

    const std::vector<TwoPortItem*>& tpsA = a->twoPorts();
    const std::vector<TwoPortItem*>& tpsB = b->twoPorts();
    if (!tpsA.empty() && !tpsB.empty()) {
        for (const TwoPortItem* twoPort : tpsA) {
            if (std::find(tpsB.begin(), tpsB.end(), twoPort) == tpsB.end()) {
                continue;
            }
            if (!isReferenceOf(twoPort, a) || !isReferenceOf(twoPort, b)) {
                return fail(tr("Within one two-port, only the two reference nodes may be combined."));
            }
        }
        const PortClass classA = portClass(a);
        if (classA != portClass(b)) {
            return fail(tr("Cascade merge: combine across with across, or reference with reference."));
        }
        if (classA == PortClass::Across && !a->isGround() && !b->isGround() &&
            a->systemType() != b->systemType()) {
            return fail(tr("Cascaded across nodes must be in the same energy domain to combine."));
        }
        return true;
    }

    if (!tpsA.empty() || !tpsB.empty()) {
        const NodeItem* portNode = tpsA.empty() ? b : a;
        const NodeItem* standalone = tpsA.empty() ? a : b;
        if (portClass(portNode) != PortClass::Reference) {
            return true;
        }
        if (!portNode->isGround() || !standalone->isGround()) {
            return fail(tr("Only reference (ground) nodes may be combined with a two-port reference."));
        }
        if (portNode->systemType() != standalone->systemType()) {
            return fail(tr("Reference nodes must be in the same energy domain to combine."));
        }
    }
    return true;
}

void GraphScene::mergeNodes(NodeItem* keep, NodeItem* remove, MergeUndoData* undo) {
    if (!keep || !remove || keep == remove) {
        return;
    }

    const std::vector<TwoPortItem*> owners = remove->twoPorts();
    if (undo) {
        undo->remove = snapshotOf(remove);
        undo->portRoles.clear();
        undo->rewiredBranchIds.clear();
        undo->destroyed.clear();
        for (TwoPortItem* twoPort : owners) {
            undo->portRoles.push_back({twoPort->serialId(), twoPort->portRoles(remove)});
        }
    }

    const bool suppressed = m_suppressGraphChange;
    m_suppressGraphChange = true;

    // Branches between the two nodes would become self-loops; everything else moves to `keep`.
    const std::vector<BranchItem*> branches = remove->branches();
    for (BranchItem* branch : branches) {
        NodeItem* other = branch->from() == remove ? branch->to() : branch->from();
        if (other == keep) {
            if (undo) {
                undo->destroyed.push_back(snapshotOf(branch));
            }
            destroyBranch(branch);
            continue;
        }
        if (undo) {
            undo->rewiredBranchIds.push_back(branch->serialId());
        }
        branch->replaceEndpoint(remove, keep);
    }

    // Re-point every two-port using `remove` (cascade siblings included; both refs collapse for a
    // 3-terminal element), so no two-port is left holding the deleted node.
    for (TwoPortItem* twoPort : owners) {
        twoPort->replacePortNode(remove, keep, twoPort->portRoles(remove));
        remove->detachTwoPort(twoPort);
        keep->attachTwoPort(twoPort);
    }

    removeFromScene(this, remove);
    delete remove;

    for (BranchItem* branch : keep->branches()) {
        reindexBranches(branch->from(), branch->to());
    }
    refreshTwoPortEgressAt(keep);
    m_suppressGraphChange = suppressed;
    notifyGraphChanged();
}

NodeItem* GraphScene::unmergeNodes(NodeItem* keep, const MergeUndoData& undo) {
    if (!keep) {
        return nullptr;
    }

    const bool suppressed = m_suppressGraphChange;
    m_suppressGraphChange = true;
    NodeItem* remove = createNodeFromSnapshot(undo.remove);

    for (const MergeUndoData::PortRoles& entry : undo.portRoles) {
        TwoPortItem* twoPort = twoPortById(entry.twoPortId);
        if (!twoPort) {
            continue;
        }
        twoPort->replacePortNode(keep, remove, entry.roles);
        remove->attachTwoPort(twoPort);
        if (twoPort->portRoles(keep) == 0) {
            keep->detachTwoPort(twoPort);
        }
    }

    for (int serialId : undo.rewiredBranchIds) {
        if (BranchItem* branch = branchBySerialId(serialId)) {
            branch->replaceEndpoint(keep, remove);
        }
    }

    for (const BranchSnapshot& snap : undo.destroyed) {
        createBranchFromSnapshot(snap);
    }

    for (NodeItem* node : {keep, remove}) {
        for (BranchItem* branch : node->branches()) {
            reindexBranches(branch->from(), branch->to());
        }
        refreshTwoPortEgressAt(node);
    }
    m_suppressGraphChange = suppressed;
    notifyGraphChanged();
    return remove;
}

bool GraphScene::tryMergeOverlappingNodes(NodeItem* moved, const QPointF& dragStart) {
    if (!moved || m_mode != Mode::Select) {
        return false;
    }
    NodeItem* other = nodeAt(moved->scenePos(), moved);
    if (!other) {
        return false;
    }
    return pushMergeNodes(moved, other, moved, dragStart);
}
