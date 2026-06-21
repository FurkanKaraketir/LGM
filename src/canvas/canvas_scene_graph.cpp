#include "canvas.h"
#include "elemental_equation.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QMessageBox>
#include <QPainter>
#include <QSet>

namespace {

void refreshTwoPortEgressAt(NodeItem* port) {
    if (!port || !port->twoPort()) {
        return;
    }
    TwoPortItem* twoPort = port->twoPort();
    for (BranchItem* branch : port->branches()) {
        if (!GraphScene::isInternalTwoPortBranch(twoPort, branch)) {
            branch->updatePath();
        }
    }
}

}  // namespace

GraphScene::GraphScene(QObject* parent) : QGraphicsScene(-2000, -2000, 4000, 4000, parent) {
    m_undoStack.setUndoLimit(200);
}

void GraphScene::notifyGraphChanged() {
    clearNormalTreeHighlight();
    if (!m_suppressGraphChange) {
        emit graphChanged();
    }
}

int GraphScene::allocateSourceInputId() {
    return m_nextSourceInputId++;
}

void GraphScene::registerSourceInputId(int id) {
    if (id >= m_nextSourceInputId) {
        m_nextSourceInputId = id + 1;
    }
}

NodeItem* GraphScene::createNode(const QPointF& center) {
    const int id = m_nextNodeId++;
    auto* node = new NodeItem;
    node->setName(tr("Node %1").arg(id));
    node->setAcrossVariable(lg::defaultNodeAcrossName(id, m_defaultSystemType));
    node->setSystemType(m_defaultSystemType);
    node->setPos(snap(center));
    addItem(node);
    notifyGraphChanged();
    return node;
}

void GraphScene::destroyNode(NodeItem* node) {
    if (!node) {
        return;
    }
    if (TwoPortItem* twoPort = node->twoPort()) {
        destroyTwoPort(twoPort);
        return;
    }
    node->setSelected(false);
    const std::vector<BranchItem*> attached = node->branches();
    m_suppressGraphChange = true;
    for (BranchItem* branch : attached) {
        destroyBranch(branch);
    }
    m_suppressGraphChange = false;
    removeItem(node);
    delete node;
    notifyGraphChanged();
}

BranchItem* GraphScene::createBranch(NodeItem* a, NodeItem* b, qreal bow) {
    if (a == b) {
        return nullptr;
    }
    const int count = static_cast<int>(branchesBetween(a, b).size()) + 1;
    auto* branch = new BranchItem(a, b, count - 1, count, bow);
    a->addBranch(branch);
    b->addBranch(branch);
    addItem(branch);
    reindexBranches(a, b);
    const int branchId = m_nextBranchId++;
    branch->setSerialId(branchId);
    branch->setName(lg::throughNameFromConstant(branch->elementConstant(), branchId,
                                                lg::branchSystemType(*branch)));
    if (a->twoPort()) {
        refreshTwoPortEgressAt(a);
    }
    if (b->twoPort() && b != a) {
        refreshTwoPortEgressAt(b);
    }
    notifyGraphChanged();
    return branch;
}

TwoPortItem* GraphScene::createTwoPort(const QPointF& center, TwoPortKind kind, const QString& modulus,
                                       const QString& name) {
    const QPointF c = snap(center);
    const QPointF v1Pos = c + QPointF(-kTwoPortHalfWidth, -kTwoPortHalfHeight);
    const QPointF v2Pos = c + QPointF(kTwoPortHalfWidth, -kTwoPortHalfHeight);
    const QPointF g1Pos = c + QPointF(-kTwoPortHalfWidth, kTwoPortHalfHeight);
    const QPointF g2Pos = c + QPointF(kTwoPortHalfWidth, kTwoPortHalfHeight);

    m_suppressGraphChange = true;
    auto* v1 = createNode(v1Pos);
    auto* v2 = createNode(v2Pos);
    auto* g1 = createNode(g1Pos);
    auto* g2 = createNode(g2Pos);
    g1->setGround(true);
    g2->setGround(true);

    auto* left = createBranch(v1, g1, kind == TwoPortKind::Gyrator ? 14.0 : -14.0);
    auto* right = createBranch(v2, g2, kind == TwoPortKind::Gyrator ? -14.0 : 14.0);

    const QString twoPortName =
        name.isEmpty() ? (kind == TwoPortKind::Transformer ? tr("Transformer %1").arg(m_nextTwoPortId)
                                                           : tr("Gyrator %1").arg(m_nextTwoPortId))
                       : name;
    ++m_nextTwoPortId;

    auto* item = new TwoPortItem(kind, c, v1, v2, g1, g2, left, right);
    item->setName(twoPortName);
    item->applyPortDefaults();
    if (!modulus.isEmpty()) {
        item->setModulus(modulus);
    }
    for (NodeItem* node : {v1, v2, g1, g2}) {
        node->setTwoPort(item);
    }
    addItem(item);
    item->refresh();
    m_suppressGraphChange = false;
    notifyGraphChanged();
    return item;
}

void GraphScene::destroyTwoPort(TwoPortItem* item) {
    if (!item) {
        return;
    }
    item->setSelected(false);

    NodeItem* nodes[] = {item->v1(), item->v2(), item->g1(), item->g2()};
    BranchItem* branches[] = {item->leftBranch(), item->rightBranch()};

    std::vector<BranchItem*> externalBranches;
    for (NodeItem* node : nodes) {
        if (!node) {
            continue;
        }
        for (BranchItem* branch : node->branches()) {
            if (!isInternalTwoPortBranch(item, branch)) {
                externalBranches.push_back(branch);
            }
        }
    }

    for (NodeItem* node : nodes) {
        if (node) {
            node->setTwoPort(nullptr);
            node->setSelected(false);
        }
    }

    removeItem(item);
    delete item;

    m_suppressGraphChange = true;
    for (BranchItem* branch : branches) {
        if (!branch) {
            continue;
        }
        NodeItem* a = branch->from();
        NodeItem* b = branch->to();
        a->removeBranch(branch);
        b->removeBranch(branch);
        removeItem(branch);
        delete branch;
    }
    for (BranchItem* branch : externalBranches) {
        NodeItem* a = branch->from();
        NodeItem* b = branch->to();
        branch->setSelected(false);
        if (a) {
            a->removeBranch(branch);
        }
        if (b) {
            b->removeBranch(branch);
        }
        removeItem(branch);
        delete branch;
    }
    m_suppressGraphChange = false;

    QSet<NodeItem*> uniqueNodes;
    for (NodeItem* node : nodes) {
        if (node) {
            uniqueNodes.insert(node);
        }
    }
    for (NodeItem* node : uniqueNodes) {
        if (node->scene()) {
            removeItem(node);
            delete node;
        }
    }
    notifyGraphChanged();
}

void GraphScene::flipBranch(BranchItem* branch) {
    if (!branch) {
        return;
    }
    branch->flip();
}

void GraphScene::destroyBranch(BranchItem* branch) {
    if (!branch) {
        return;
    }
    if (TwoPortItem* twoPort = twoPortFor(branch)) {
        if (isInternalTwoPortBranch(twoPort, branch)) {
            destroyTwoPort(twoPort);
            return;
        }
    }
    branch->setSelected(false);
    NodeItem* a = branch->from();
    NodeItem* b = branch->to();
    a->removeBranch(branch);
    b->removeBranch(branch);
    removeItem(branch);
    delete branch;
    if (a->scene() && b->scene()) {
        reindexBranches(a, b);
    }
    if (a->twoPort()) {
        refreshTwoPortEgressAt(a);
    }
    if (b->twoPort() && b != a) {
        refreshTwoPortEgressAt(b);
    }
    notifyGraphChanged();
}

void GraphScene::destroyBranchAt(const BranchKey& key) {
    NodeItem* a = nodeAtPos(key.from);
    NodeItem* b = nodeAtPos(key.to);
    if (!a || !b) {
        return;
    }
    for (BranchItem* branch : branchesBetween(a, b)) {
        if (branch->index() == key.index) {
            if (TwoPortItem* twoPort = twoPortFor(branch)) {
                if (isInternalTwoPortBranch(twoPort, branch)) {
                    return;
                }
            }
            destroyBranch(branch);
            return;
        }
    }
}

NodeItem* GraphScene::nodeAtPos(const QPointF& pos) const {
    for (QGraphicsItem* item : items()) {
        auto* node = dynamic_cast<NodeItem*>(item);
        if (node && node->scenePos() == pos) {
            return node;
        }
    }
    return nullptr;
}

TwoPortItem* GraphScene::twoPortAtCenter(const QPointF& center) const {
    const QPointF snapped = snap(center);
    for (QGraphicsItem* item : items()) {
        auto* twoPort = dynamic_cast<TwoPortItem*>(item);
        if (twoPort && twoPort->center() == snapped) {
            return twoPort;
        }
    }
    return nullptr;
}

TwoPortItem* GraphScene::twoPortFor(const QGraphicsItem* item) const {
    if (const auto* twoPort = dynamic_cast<const TwoPortItem*>(item)) {
        return const_cast<TwoPortItem*>(twoPort);
    }
    if (const auto* node = dynamic_cast<const NodeItem*>(item)) {
        return node->twoPort();
    }
    if (const auto* branch = dynamic_cast<const BranchItem*>(item)) {
        if (NodeItem* from = branch->from()) {
            return from->twoPort();
        }
    }
    return nullptr;
}

TwoPortItem* GraphScene::twoPortAt(const QPointF& scenePos) const {
    for (QGraphicsItem* item : items(scenePos)) {
        if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            return twoPort;
        }
    }
    return nullptr;
}

void GraphScene::selectTwoPort(TwoPortItem* item) {
    clearSelection();
    if (item) {
        item->selectMembers(true);
    }
}

void GraphScene::selectTwoPortNode(NodeItem* node) {
    if (!node || !node->twoPort()) {
        return;
    }
    clearSelection();
    node->setSelected(true);
    node->twoPort()->setSelected(true);
}

QRectF GraphScene::contentBounds() const {
    QRectF bounds;
    bool any = false;
    for (QGraphicsItem* item : items()) {
        if (!dynamic_cast<NodeItem*>(item) && !dynamic_cast<BranchItem*>(item) &&
            !dynamic_cast<TwoPortItem*>(item)) {
            continue;
        }
        const QRectF rect = item->sceneBoundingRect();
        bounds = any ? bounds.united(rect) : rect;
        any = true;
    }
    if (!any) {
        return {};
    }
    return bounds;
}

std::vector<BranchItem*> GraphScene::branchesBetween(NodeItem* a, NodeItem* b) const {
    std::vector<BranchItem*> result;
    for (QGraphicsItem* item : items()) {
        auto* branch = dynamic_cast<BranchItem*>(item);
        if (!branch) {
            continue;
        }
        if ((branch->from() == a && branch->to() == b) || (branch->from() == b && branch->to() == a)) {
            result.push_back(branch);
        }
    }
    return result;
}

void GraphScene::reindexBranches(NodeItem* a, NodeItem* b) {
    if (!a || !b || !a->scene() || !b->scene()) {
        return;
    }
    std::vector<BranchItem*> branches = branchesBetween(a, b);
    const int count = static_cast<int>(branches.size());
    for (int i = 0; i < count; ++i) {
        branches[i]->setSlot(i, count);
    }
}

void GraphScene::setMode(Mode mode) {
    if (m_mode == mode) {
        return;
    }
    if (m_mode == Mode::AddBranch) {
        clearBranchPending();
    }
    m_mode = mode;
    emit modeChanged(mode);
}

void GraphScene::setDefaultSystemType(SystemType type) {
    m_defaultSystemType = type;
}

void GraphScene::setSnapToGrid(bool enabled) {
    m_snapToGrid = enabled;
}

void GraphScene::setShowGrid(bool enabled) {
    if (m_showGrid == enabled) {
        return;
    }
    m_showGrid = enabled;
    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
}

void GraphScene::setGridSpacing(qreal spacing) {
    m_gridSpacing = std::max<qreal>(1.0, spacing);
    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
}

void GraphScene::refreshAppearance() {
    for (QGraphicsItem* item : items()) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            node->refreshTheme();
        } else if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branch->refreshTheme();
        } else if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            twoPort->update();
        }
    }
    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
}

void GraphScene::clearBranchPending() {
    if (m_pending) {
        if (m_pending->scene()) {
            m_pending->setSelected(false);
        }
        m_pending = nullptr;
    }
}

BranchItem* GraphScene::branchAt(const QPointF& scenePos) const {
    for (QGraphicsItem* item : items(scenePos)) {
        if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            return branch;
        }
    }
    return nullptr;
}

NodeItem* GraphScene::nodeAt(const QPointF& scenePos, const NodeItem* except) const {
    for (QGraphicsItem* item : items(scenePos)) {
        auto* node = dynamic_cast<NodeItem*>(item);
        if (node && node != except) {
            return node;
        }
    }
    return nullptr;
}

QPointF GraphScene::snap(QPointF point) const {
    if (!m_snapToGrid) {
        return point;
    }
    return {std::round(point.x() / m_gridSpacing) * m_gridSpacing,
            std::round(point.y() / m_gridSpacing) * m_gridSpacing};
}

void GraphScene::clearNormalTreeHighlight() {
    if (!m_normalTreeHighlightActive) {
        return;
    }
    m_normalTreeHighlightActive = false;
    m_lastNormalTreeResult = {};
    m_lastStateSpaceResult = {};
    for (QGraphicsItem* item : items()) {
        if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branch->setNormalTreeRole(false, false);
        }
    }
}

lg::NormalTreeResult GraphScene::findNormalTree() {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    nodes.reserve(static_cast<size_t>(items().size()));
    branches.reserve(static_cast<size_t>(items().size()));
    twoPorts.reserve(static_cast<size_t>(items().size()));
    for (QGraphicsItem* item : items()) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            nodes.push_back(node);
        } else if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branches.push_back(branch);
        } else if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            twoPorts.push_back(twoPort);
        }
    }

    for (QGraphicsItem* item : items()) {
        if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branch->setNormalTreeRole(false, false);
        }
    }
    m_normalTreeHighlightActive = false;
    m_lastNormalTreeResult = {};
    m_lastStateSpaceResult = {};

    lg::NormalTreeResult result = lg::computeNormalTree(nodes, branches, twoPorts);
    if (result.status != lg::NormalTreeResult::Status::Ok) {
        return result;
    }

    lg::populateNormalTreeStateVariables(result, branches);
    m_normalTreeHighlightActive = true;
    m_lastNormalTreeResult = result;
    for (BranchItem* branch : branches) {
        const bool inTree =
            std::find(result.treeBranches.begin(), result.treeBranches.end(), branch) !=
            result.treeBranches.end();
        branch->setNormalTreeRole(inTree, true);
    }
    return result;
}

lg::StateSpaceResult GraphScene::computeStateSpaceRep() {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    for (QGraphicsItem* item : items()) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            nodes.push_back(node);
        } else if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branches.push_back(branch);
        } else if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            twoPorts.push_back(twoPort);
        }
    }

    if (!m_normalTreeHighlightActive ||
        m_lastNormalTreeResult.status != lg::NormalTreeResult::Status::Ok) {
        lg::StateSpaceResult result;
        result.status = lg::StateSpaceResult::Status::NeedNormalTree;
        result.message = QStringLiteral("Find a valid normal tree first.");
        m_lastStateSpaceResult = result;
        return result;
    }

    m_lastStateSpaceResult =
        lg::computeStateSpace(m_lastNormalTreeResult, nodes, branches, twoPorts);
    return m_lastStateSpaceResult;
}

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

    const std::vector<BranchItem*> branches = remove->branches();
    for (BranchItem* branch : branches) {
        if (tpKeep && tpKeep == tpRemove && isInternalTwoPortBranch(tpKeep, branch) && sharedRef) {
            continue;
        }
        NodeItem* other = branch->from() == remove ? branch->to() : branch->from();
        if (other == keep) {
            if (undo) {
                MergeUndoData::Destroyed snap;
                snap.key = branchKeyOf(branch);
                snap.name = branch->name();
                snap.active = branch->isActive();
                snap.type = branch->branchType();
                snap.constant = branch->elementConstant();
                undo->destroyed.push_back(snap);
            }
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
    removeItem(remove);
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
            BranchItem* branch = createBranch(a, b);
            if (branch) {
                branch->setName(snap.name);
                branch->setActive(snap.active);
                branch->setBranchType(snap.type);
                branch->setElementConstant(snap.constant);
            }
        }
    }

    m_suppressGraphChange = false;
    notifyGraphChanged();
}

void GraphScene::tryMergeOverlappingNodes(NodeItem* moved) {
    if (!moved || m_mode != Mode::Select) {
        return;
    }
    NodeItem* other = nodeAt(moved->scenePos(), moved);
    if (!other) {
        return;
    }
    pushMergeNodes(moved, other);
}
