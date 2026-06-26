#include "canvas.h"
#include "scene_detail.h"

#include "elemental_equation.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <vector>

#include <QMessageBox>
#include <QPainter>
#include <QSet>

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
        purgeTwoPort(twoPort);
        return;
    }
    node->setSelected(false);
    const std::vector<BranchItem*> attached = node->branches();
    for (BranchItem* branch : attached) {
        destroyBranch(branch);
    }
    removeFromScene(this, node);
    delete node;
    if (!m_suppressGraphChange) {
        notifyGraphChanged();
    }
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
    branch->setElementConstant(
        lg::defaultElementConstant(branchId, lg::branchSystemType(*branch)));
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

    auto* left = createBranch(v1, g1, 28.0);
    auto* right = createBranch(v2, g2, -28.0);
    left->setTwoPortPort(true);
    right->setTwoPortPort(true);

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

TwoPortItem* GraphScene::createTwoPortFromPorts(NodeItem* v1, NodeItem* v2, NodeItem* g1, NodeItem* g2,
                                                TwoPortKind kind, const QString& modulus,
                                                const QString& name) {
    if (!v1 || !v2 || !g1 || !g2) {
        return nullptr;
    }

    m_suppressGraphChange = true;
    const auto createPortBranch = [&](NodeItem* a, NodeItem* b, qreal bow) -> BranchItem* {
        BranchItem* branch = createBranch(a, b, bow);
        if (!branch) {
            return nullptr;
        }
        branch->setTwoPortPort(true);
        branch->setBranchType(BranchType::T);
        branch->setActive(false);
        branch->setElementConstant(QStringLiteral("1"));
        return branch;
    };
    auto* left = createPortBranch(v1, g1, 28.0);
    auto* right = createPortBranch(v2, g2, -28.0);
    if (!left || !right) {
        m_suppressGraphChange = false;
        return nullptr;
    }

    const QPointF c = snap((v1->scenePos() + v2->scenePos() + g1->scenePos() + g2->scenePos()) / 4.0);
    const QString twoPortName =
        name.isEmpty() ? (kind == TwoPortKind::Transformer ? tr("Transformer %1").arg(m_nextTwoPortId)
                                                           : tr("Gyrator %1").arg(m_nextTwoPortId))
                       : name;
    ++m_nextTwoPortId;

    auto* item = new TwoPortItem(kind, c, v1, v2, g1, g2, left, right);
    item->setName(twoPortName);
    if (!modulus.isEmpty()) {
        item->setModulus(modulus);
    }
    for (NodeItem* node : {v1, v2, g1, g2}) {
        if (node && !node->twoPort()) {
            node->setTwoPort(item);
        }
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

    for (NodeItem* node : {item->v1(), item->v2(), item->g1(), item->g2()}) {
        if (node) {
            node->setTwoPort(nullptr);
            node->setSelected(false);
        }
    }

    BranchItem* internal[] = {item->leftBranch(), item->rightBranch()};

    removeFromScene(this, item);
    delete item;

    QSet<BranchItem*> removed;
    for (BranchItem* branch : internal) {
        if (!branch || removed.contains(branch)) {
            continue;
        }
        NodeItem* a = branch->from();
        NodeItem* b = branch->to();
        if (a) {
            a->removeBranch(branch);
        }
        if (b) {
            b->removeBranch(branch);
        }
        removeFromScene(this, branch);
        delete branch;
        removed.insert(branch);
    }
    if (!m_suppressGraphChange) {
        notifyGraphChanged();
    }
}

void GraphScene::purgeTwoPort(TwoPortItem* item) {
    if (!item) {
        return;
    }
    QSet<NodeItem*> portNodes;
    for (NodeItem* node : {item->v1(), item->v2(), item->g1(), item->g2()}) {
        if (node) {
            portNodes.insert(node);
        }
    }
    destroyTwoPort(item);
    for (NodeItem* node : portNodes) {
        if (node->scene() == this) {
            destroyNode(node);
        }
    }
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
            purgeTwoPort(twoPort);
            return;
        }
    }
    branch->setSelected(false);
    NodeItem* a = branch->from();
    NodeItem* b = branch->to();
    if (!a || !b) {
        removeFromScene(this, branch);
        delete branch;
        if (!m_suppressGraphChange) {
            notifyGraphChanged();
        }
        return;
    }
    a->removeBranch(branch);
    b->removeBranch(branch);
    removeFromScene(this, branch);
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
    if (!m_suppressGraphChange) {
        notifyGraphChanged();
    }
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
    if (const auto* branch = dynamic_cast<const BranchItem*>(item)) {
        for (QGraphicsItem* sceneItem : items()) {
            if (auto* twoPort = dynamic_cast<TwoPortItem*>(sceneItem)) {
                if (isInternalTwoPortBranch(twoPort, const_cast<BranchItem*>(branch))) {
                    return twoPort;
                }
            }
        }
        return nullptr;
    }
    if (const auto* node = dynamic_cast<const NodeItem*>(item)) {
        return twoPortForNode(node);
    }
    return nullptr;
}

TwoPortItem* GraphScene::twoPortForNode(const NodeItem* node, TwoPortItem* prefer,
                                        const QPointF& sceneHint) const {
    if (!node) {
        return nullptr;
    }

    QVector<TwoPortItem*> matches;
    for (QGraphicsItem* sceneItem : items()) {
        if (auto* twoPort = dynamic_cast<TwoPortItem*>(sceneItem)) {
            if (isTwoPortPortNode(twoPort, node)) {
                matches.push_back(twoPort);
            }
        }
    }

    if (matches.isEmpty()) {
        return node->twoPort();
    }
    if (matches.size() == 1) {
        return matches.front();
    }
    if (prefer && matches.contains(prefer)) {
        return prefer;
    }
    if (!sceneHint.isNull()) {
        if (TwoPortItem* at = twoPortAt(sceneHint); at && matches.contains(at)) {
            return at;
        }
        TwoPortItem* best = nullptr;
        qreal bestDist = std::numeric_limits<qreal>::max();
        for (TwoPortItem* twoPort : matches) {
            const qreal dist = QLineF(sceneHint, twoPort->center()).length();
            if (dist < bestDist) {
                bestDist = dist;
                best = twoPort;
            }
        }
        if (best) {
            return best;
        }
    }
    if (TwoPortItem* owned = node->twoPort(); owned && matches.contains(owned)) {
        return owned;
    }
    return matches.front();
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

void GraphScene::selectTwoPortNode(NodeItem* node, TwoPortItem* twoPort) {
    if (!node) {
        return;
    }
    if (!twoPort) {
        twoPort = twoPortForNode(node);
    }
    if (!twoPort || !isTwoPortPortNode(twoPort, node)) {
        return;
    }
    clearSelection();
    node->setSelected(true);
    twoPort->setSelected(true);
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
    if (m_mode == Mode::SelectNormalTree && mode != Mode::SelectNormalTree) {
        leaveManualNormalTreeMode(true);
        if (mode == Mode::Select) {
            return;
        }
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

