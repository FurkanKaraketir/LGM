#include "canvas.h"

#include <cmath>
#include <vector>

GraphScene::GraphScene(QObject* parent) : QGraphicsScene(-2000, -2000, 4000, 4000, parent) {}

void GraphScene::notifyGraphChanged() {
    if (!m_suppressGraphChange) {
        emit graphChanged();
    }
}

NodeItem* GraphScene::createNode(const QPointF& center) {
    auto* node = new NodeItem;
    node->setName(tr("Node %1").arg(m_nextNodeId++));
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
    branch->setName(tr("Branch %1").arg(m_nextBranchId++));
    notifyGraphChanged();
    return branch;
}

TwoPortItem* GraphScene::createTwoPort(const QPointF& center, TwoPortKind kind) {
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
    v1->setName(tr("V₁"));
    v2->setName(tr("V₂"));
    g1->setName(tr("G₁"));
    g2->setName(tr("G₂"));
    g1->setGround(true);
    g2->setGround(true);

    auto* left = createBranch(v1, g1, -14.0);
    auto* right = createBranch(v2, g2, 14.0);
    left->setName(tr("Left branch"));
    right->setName(tr("Right branch"));

    const QString twoPortName =
        kind == TwoPortKind::Transformer ? tr("Transformer %1").arg(m_nextTwoPortId)
                                         : tr("Gyrator %1").arg(m_nextTwoPortId);
    ++m_nextTwoPortId;

    auto* item = new TwoPortItem(kind, c, v1, v2, g1, g2, left, right);
    item->setName(twoPortName);
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

    for (NodeItem* node : nodes) {
        if (node) {
            node->setTwoPort(nullptr);
            node->setSelected(false);
        }
    }

    removeItem(item);
    delete item;

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

    for (NodeItem* node : nodes) {
        if (node && node->scene()) {
            removeItem(node);
            delete node;
        }
    }
    notifyGraphChanged();
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

NodeItem* GraphScene::nodeAt(const QPointF& scenePos) const {
    for (QGraphicsItem* item : items(scenePos)) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            return node;
        }
    }
    return nullptr;
}

QPointF GraphScene::snap(QPointF point) const {
    return {std::round(point.x() / kGrid) * kGrid, std::round(point.y() / kGrid) * kGrid};
}
