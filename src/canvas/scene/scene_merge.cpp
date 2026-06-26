#include "canvas.h"
#include "scene_detail.h"

namespace {

int twoPortRole(const TwoPortItem* tp, const NodeItem* node) {
    if (!tp || !node) {
        return 0;
    }
    if (node == tp->v1()) {
        return 1;
    }
    if (node == tp->v2()) {
        return 2;
    }
    if (node == tp->g1()) {
        return 3;
    }
    if (node == tp->g2()) {
        return 4;
    }
    return 0;
}

NodeItem* chooseMergeKeep(NodeItem* a, NodeItem* b) {
    TwoPortItem* tpA = a->twoPort();
    TwoPortItem* tpB = b->twoPort();
    if (tpA && tpA == tpB) {
        if (a == tpA->g1() || b == tpA->g2()) {
            return tpA->g1();
        }
        if (b == tpA->g1() || a == tpA->g2()) {
            return tpA->g1();
        }
    }
    if (tpA && !tpB) {
        return a;
    }
    if (tpB && !tpA) {
        return b;
    }
    return a;
}

GraphScene::BranchKey branchKeyOf(const BranchItem* branch) {
    return {branch->from()->scenePos(), branch->to()->scenePos(), branch->index()};
}

void reassignTwoPortEndpoint(TwoPortItem* tp, NodeItem* from, NodeItem* to) {
    if (!tp || !from || !to || from == to) {
        return;
    }
    if (tp->v1() == from) {
        tp->setV1(to);
    } else if (tp->v2() == from) {
        tp->setV2(to);
    } else if (tp->g1() == from) {
        tp->setG1(to);
    } else if (tp->g2() == from) {
        tp->setG2(to);
    }
    tp->refresh();
}

}  // namespace

bool GraphScene::canMergeNodes(const NodeItem* a, const NodeItem* b, QString* reason) const {
    if (!a || !b || a == b) {
        if (reason) {
            *reason = tr("Select two distinct nodes.");
        }
        return false;
    }
    const TwoPortItem* tpA = a->twoPort();
    const TwoPortItem* tpB = b->twoPort();
    if (tpA && tpB) {
        if (tpA != tpB) {
            const int roleA = twoPortRole(tpA, a);
            const int roleB = twoPortRole(tpB, b);
            const bool acrossA = roleA >= 1 && roleA <= 2;
            const bool acrossB = roleB >= 1 && roleB <= 2;
            const bool refA = roleA >= 3 && roleA <= 4;
            const bool refB = roleB >= 3 && roleB <= 4;
            if ((acrossA && acrossB) || (refA && refB)) {
                return true;
            }
            if (reason) {
                *reason = tr("Cascade merge: combine across with across, or reference with reference.");
            }
            return false;
        }
        if (twoPortRole(tpA, a) < 3 || twoPortRole(tpA, b) < 3) {
            if (reason) {
                *reason = tr("Within one two-port, only the two reference nodes may be combined.");
            }
            return false;
        }
        if (tpA->hasSharedReference()) {
            if (reason) {
                *reason = tr("This two-port already uses a shared reference node.");
            }
            return false;
        }
        return true;
    }
    if (tpA && twoPortRole(tpA, a) <= 2 && !tpB) {
        return true;
    }
    if (tpB && twoPortRole(tpB, b) <= 2 && !tpA) {
        return true;
    }
    if (tpA || tpB) {
        if (reason) {
            *reason = tr("This two-port node cannot be removed by combining.");
        }
        return false;
    }
    return true;
}

void GraphScene::mergeNodes(NodeItem* keep, NodeItem* remove, MergeUndoData* undo) {
    if (!keep || !remove || keep == remove) {
        return;
    }

    TwoPortItem* tpKeep = keep->twoPort();
    TwoPortItem* tpRemove = remove->twoPort();
    const bool sharedRef = tpKeep && tpKeep == tpRemove && twoPortRole(tpKeep, keep) >= 3 &&
                           twoPortRole(tpKeep, remove) >= 3;

    if (undo) {
        undo->removePos = remove->scenePos();
        undo->removeName = remove->name();
        undo->removeAcross = remove->isGround() ? QStringLiteral("0") : remove->acrossVariable();
        undo->removeGround = remove->isGround();
        undo->twoPort = tpRemove;
        undo->removeRole = twoPortRole(tpRemove, remove);
        undo->collapsedSharedRef = sharedRef;
        undo->rewired.clear();
        undo->destroyed.clear();
    }
    const auto snapshotDestroyed = [&](BranchItem* branch) {
        if (!undo || !branch || !branch->from() || !branch->to()) {
            return;
        }
        MergeUndoData::Destroyed snap;
        snap.key = branchKeyOf(branch);
        snap.name = branch->name();
        snap.active = branch->isActive();
        snap.type = branch->branchType();
        snap.constant = branch->elementConstant();
        snap.bow = branch->bow();
        undo->destroyed.push_back(snap);
    };

    const std::vector<BranchItem*> branches = remove->branches();
    for (BranchItem* branch : branches) {
        if (tpKeep && tpKeep == tpRemove && isInternalTwoPortBranch(tpKeep, branch) && sharedRef) {
            continue;
        }
        NodeItem* other = branch->from() == remove ? branch->to() : branch->from();
        if (other == keep) {
            snapshotDestroyed(branch);
            destroyBranch(branch);
            continue;
        }
        if (undo) {
            MergeUndoData::Rewire snap;
            snap.removeWasFrom = branch->from() == remove;
            undo->rewired.push_back(snap);
        }
        branch->replaceEndpoint(remove, keep);
        if (undo && !undo->rewired.empty()) {
            undo->rewired.back().key = branchKeyOf(branch);
        }
    }

    if (sharedRef) {
        tpKeep->collapseSharedRef(keep, remove);
    } else if (tpRemove) {
        reassignTwoPortEndpoint(tpRemove, remove, keep);
    }

    remove->setTwoPort(nullptr);
    removeFromScene(this, remove);
    delete remove;

    QSet<NodeItem*> touched;
    for (BranchItem* branch : keep->branches()) {
        if (branch->from()) {
            touched.insert(branch->from());
        }
        if (branch->to()) {
            touched.insert(branch->to());
        }
    }
    for (NodeItem* node : touched) {
        for (BranchItem* branch : node->branches()) {
            if (branch->from() && branch->to()) {
                reindexBranches(branch->from(), branch->to());
            }
        }
    }
    if (keep->twoPort()) {
        refreshTwoPortEgressAt(keep);
    }
    notifyGraphChanged();
}

void GraphScene::unmergeNodes(NodeItem* keep, const MergeUndoData& undo) {
    if (!keep) {
        return;
    }

    m_suppressGraphChange = true;
    auto* remove = createNode(undo.removePos);
    remove->setName(undo.removeName);
    if (undo.removeGround) {
        remove->setGround(true);
    } else {
        remove->setAcrossVariable(undo.removeAcross);
    }

    if (undo.twoPort && undo.removeRole > 0) {
        remove->setTwoPort(undo.twoPort);
        switch (undo.removeRole) {
        case 1:
            undo.twoPort->setV1(remove);
            break;
        case 2:
            undo.twoPort->setV2(remove);
            break;
        case 3:
            undo.twoPort->setG1(remove);
            break;
        case 4:
            undo.twoPort->setG2(remove);
            break;
        default:
            break;
        }
        if (undo.collapsedSharedRef) {
            if (undo.removeRole == 4) {
                undo.twoPort->rightBranch()->replaceEndpoint(keep, remove);
            } else if (undo.removeRole == 3) {
                undo.twoPort->leftBranch()->replaceEndpoint(keep, remove);
            }
        }
        undo.twoPort->refresh();
    }

    for (const MergeUndoData::Rewire& snap : undo.rewired) {
        NodeItem* a = nodeAtPos(snap.key.from);
        NodeItem* b = nodeAtPos(snap.key.to);
        if (!a || !b) {
            continue;
        }
        for (BranchItem* branch : branchesBetween(a, b)) {
            if (branch->index() != snap.key.index) {
                continue;
            }
            if (snap.removeWasFrom) {
                if (branch->from() == keep) {
                    branch->replaceEndpoint(keep, remove);
                }
            } else if (branch->to() == keep) {
                branch->replaceEndpoint(keep, remove);
            }
            break;
        }
    }

    for (const MergeUndoData::Destroyed& snap : undo.destroyed) {
        NodeItem* a = nodeAtPos(snap.key.from);
        NodeItem* b = nodeAtPos(snap.key.to);
        if (a && b) {
            BranchItem* branch = createBranch(a, b, snap.bow);
            if (branch) {
                branch->setActive(snap.active);
                branch->setElementConstant(snap.constant);
                branch->setBranchType(snap.type);
                branch->setName(snap.name);
            }
        }
    }

    m_suppressGraphChange = false;
    notifyGraphChanged();
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
