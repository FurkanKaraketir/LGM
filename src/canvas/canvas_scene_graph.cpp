#include "canvas.h"
#include "elemental_equation.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
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
    twoPort->refresh();
    for (BranchItem* branch : port->branches()) {
        if (!GraphScene::isInternalTwoPortBranch(twoPort, branch)) {
            branch->updatePath();
        }
    }
}

void removeFromScene(GraphScene* scene, QGraphicsItem* item) {
    if (item && item->scene() == scene) {
        scene->removeItem(item);
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

void GraphScene::clearNormalTreeHighlight() {
    if (!m_normalTreeHighlightActive && m_mode != Mode::SelectNormalTree) {
        return;
    }
    const bool hadTree = m_normalTreeHighlightActive;
    m_normalTreeHighlightActive = false;
    m_normalTreeManual = false;
    m_lastNormalTreeResult = {};
    m_lastStateSpaceResult = {};
    m_manualNormalTreeValidation = {};
    const bool hadDiscovered = !m_discoveredNormalTrees.empty();
    m_discoveredNormalTrees.clear();
    m_discoveredNormalTreeIndex = -1;
    for (QGraphicsItem* item : items()) {
        if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branch->setNormalTreeRole(false, false);
        }
    }
    if (m_mode == Mode::SelectNormalTree) {
        m_manualNormalTreeBackup = {};
        m_mode = Mode::Select;
        setNormalTreePickingPassthrough(false);
        emit modeChanged(Mode::Select);
    }
    if (hadTree) {
        m_activeSavedNormalTreeIndex = -1;
        emit normalTreeHighlightChanged();
    }
    if (hadDiscovered) {
        emit discoveredNormalTreesChanged();
    }
}

void GraphScene::collectGraphItems(std::vector<NodeItem*>& nodes, std::vector<BranchItem*>& branches,
                                   std::vector<TwoPortItem*>& twoPorts) const {
    nodes.clear();
    branches.clear();
    twoPorts.clear();
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
}

void GraphScene::refreshManualNormalTreeValidation() {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    collectGraphItems(nodes, branches, twoPorts);

    std::vector<BranchItem*> treeBranches;
    treeBranches.reserve(branches.size());
    for (BranchItem* branch : branches) {
        if (branch && branch->normalTreeRoleKnown() && branch->inNormalTree()) {
            treeBranches.push_back(branch);
        }
    }

    m_manualNormalTreeValidation =
        lg::validateManualNormalTree(nodes, branches, twoPorts, treeBranches);
    if (m_manualNormalTreeValidation.status == lg::NormalTreeResult::Status::Ok) {
        m_lastNormalTreeResult = m_manualNormalTreeValidation;
        m_lastStateSpaceResult = {};
        m_normalTreeHighlightActive = true;
    }
    emit manualNormalTreeValidationChanged(m_manualNormalTreeValidation);
    emit normalTreeHighlightChanged();
}

void GraphScene::restoreManualNormalTreeBackup() {
    m_normalTreeHighlightActive = m_manualNormalTreeBackup.highlightActive;
    m_lastNormalTreeResult = m_manualNormalTreeBackup.result;
    m_lastStateSpaceResult = {};
    m_manualNormalTreeValidation = {};

    const std::unordered_set<BranchItem*> inTree(m_lastNormalTreeResult.treeBranches.begin(),
                                                 m_lastNormalTreeResult.treeBranches.end());
    for (QGraphicsItem* item : items()) {
        auto* branch = dynamic_cast<BranchItem*>(item);
        if (!branch) {
            continue;
        }
        if (!m_normalTreeHighlightActive) {
            branch->setNormalTreeRole(false, false);
            continue;
        }
        branch->setNormalTreeRole(inTree.count(branch) != 0, true);
    }
    emit normalTreeHighlightChanged();
}

void GraphScene::leaveManualNormalTreeMode(bool accept) {
    if (m_mode != Mode::SelectNormalTree) {
        return;
    }

    refreshManualNormalTreeValidation();
    const lg::NormalTreeResult validation = m_manualNormalTreeValidation;

    // ponytail: leave manual mode before signals — manualNormalTreeAccepted calls
    // setToolMode(Select), which would re-enter here with cleared validation.
    m_mode = Mode::Select;
    setNormalTreePickingPassthrough(false);

    if (accept && validation.status == lg::NormalTreeResult::Status::Ok) {
        m_normalTreeManual = true;
        m_manualNormalTreeBackup = {};
        m_manualNormalTreeValidation = {};
        syncActiveSavedNormalTreeIndex();
        emit manualNormalTreeAccepted(m_lastNormalTreeResult);
    } else if (accept) {
        const QString message =
            validation.message.isEmpty()
                ? QStringLiteral("The manual normal tree selection is not valid.")
                : validation.message;
        restoreManualNormalTreeBackup();
        m_manualNormalTreeValidation = {};
        emit manualNormalTreeRejected(message);
    } else {
        restoreManualNormalTreeBackup();
        m_manualNormalTreeValidation = {};
    }

    emit modeChanged(Mode::Select);
}

void GraphScene::acceptManualNormalTreeSelection() {
    leaveManualNormalTreeMode(true);
}

void GraphScene::cancelManualNormalTreeSelection() {
    leaveManualNormalTreeMode(false);
}

void GraphScene::setNormalTreePickingPassthrough(bool enabled) {
    // ponytail: coupler shape sits above branches (z=2); pass clicks through in manual tree mode
    const Qt::MouseButtons buttons = enabled ? Qt::NoButton : Qt::LeftButton;
    for (QGraphicsItem* item : items()) {
        if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            twoPort->setAcceptedMouseButtons(buttons);
            continue;
        }
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            if (node->twoPort()) {
                node->setAcceptedMouseButtons(buttons);
            }
        }
    }
}

void GraphScene::setManualNormalTreeBranchRole(BranchItem* branch, bool inTree) {
    if (!branch || m_mode != Mode::SelectNormalTree) {
        return;
    }
    if (branch->isActive() && branch->branchType() == BranchType::A && !inTree) {
        return;
    }

    if (TwoPortItem* twoPort = twoPortFor(branch)) {
        if (isInternalTwoPortBranch(twoPort, branch)) {
            if (twoPort->kind() == TwoPortKind::Gyrator) {
                if (BranchItem* left = twoPort->leftBranch()) {
                    left->setNormalTreeRole(inTree, true);
                }
                if (BranchItem* right = twoPort->rightBranch()) {
                    right->setNormalTreeRole(inTree, true);
                }
                refreshManualNormalTreeValidation();
                return;
            }
            BranchItem* left = twoPort->leftBranch();
            BranchItem* right = twoPort->rightBranch();
            if (inTree) {
                if (branch == right) {
                    if (left) {
                        left->setNormalTreeRole(false, true);
                    }
                    branch->setNormalTreeRole(true, true);
                } else {
                    if (right) {
                        right->setNormalTreeRole(false, true);
                    }
                    branch->setNormalTreeRole(true, true);
                }
            } else if (branch->inNormalTree()) {
                branch->setNormalTreeRole(false, true);
            }
            refreshManualNormalTreeValidation();
            return;
        }
    }

    branch->setNormalTreeRole(inTree, true);
    refreshManualNormalTreeValidation();
}

void GraphScene::toggleManualNormalTreeBranch(BranchItem* branch) {
    if (!branch || m_mode != Mode::SelectNormalTree) {
        return;
    }
    setManualNormalTreeBranchRole(branch, !branch->inNormalTree());
}

void GraphScene::beginManualNormalTreeSelection() {
    m_manualNormalTreeBackup.highlightActive = m_normalTreeHighlightActive;
    m_manualNormalTreeBackup.result = m_lastNormalTreeResult;

    const lg::NormalTreeResult seed = findNormalTree();
    if (seed.status != lg::NormalTreeResult::Status::Ok) {
        std::vector<NodeItem*> nodes;
        std::vector<BranchItem*> branches;
        std::vector<TwoPortItem*> twoPorts;
        collectGraphItems(nodes, branches, twoPorts);
        m_normalTreeHighlightActive = true;
        for (BranchItem* branch : branches) {
            if (!branch) {
                continue;
            }
            const bool inTree = branch->isActive() && branch->branchType() == BranchType::A;
            branch->setNormalTreeRole(inTree, true);
        }
        m_lastNormalTreeResult = {};
        m_lastStateSpaceResult = {};
    }

    if (m_mode == Mode::AddBranch) {
        clearBranchPending();
    }
    m_mode = Mode::SelectNormalTree;
    emit modeChanged(Mode::SelectNormalTree);
    setNormalTreePickingPassthrough(true);
    refreshManualNormalTreeValidation();
}

QString GraphScene::takeLoadWarning() {
    const QString warning = m_loadWarning;
    m_loadWarning.clear();
    return warning;
}

void GraphScene::applyNormalTreeHighlight(const lg::NormalTreeResult& result) {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    collectGraphItems(nodes, branches, twoPorts);

    m_normalTreeHighlightActive = true;
    m_lastNormalTreeResult = result;
    m_lastStateSpaceResult = {};
    for (BranchItem* branch : branches) {
        if (!branch) {
            continue;
        }
        const bool inTree =
            std::find(result.treeBranches.begin(), result.treeBranches.end(), branch) !=
            result.treeBranches.end();
        branch->setNormalTreeRole(inTree, true);
    }
    syncActiveSavedNormalTreeIndex();
    emit normalTreeHighlightChanged();
}

lg::NormalTreeEnumerationResult GraphScene::findAllNormalTrees() {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    collectGraphItems(nodes, branches, twoPorts);

    for (QGraphicsItem* item : items()) {
        if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branch->setNormalTreeRole(false, false);
        }
    }
    m_normalTreeHighlightActive = false;
    m_normalTreeManual = false;
    m_lastNormalTreeResult = {};
    m_lastStateSpaceResult = {};
    m_discoveredNormalTrees.clear();
    m_discoveredNormalTreeIndex = -1;

    const lg::NormalTreeEnumerationResult enumeration =
        lg::enumerateNormalTrees(nodes, branches, twoPorts);
    m_discoveredNormalTrees = enumeration.trees;
    emit discoveredNormalTreesChanged();

    if (m_discoveredNormalTrees.empty()) {
        lg::NormalTreeEnumerationResult failure = enumeration;
        failure.trees.clear();
        return failure;
    }

    m_discoveredNormalTreeIndex = 0;
    m_normalTreeManual = false;
    applyNormalTreeHighlight(m_discoveredNormalTrees.front());
    return enumeration;
}

lg::NormalTreeResult GraphScene::findNormalTree() {
    const lg::NormalTreeEnumerationResult all = findAllNormalTrees();
    lg::NormalTreeResult result;
    if (all.trees.empty()) {
        result.status = all.status;
        result.message = all.message;
        return result;
    }
    return all.trees.front();
}

bool GraphScene::showDiscoveredNormalTree(int index) {
    if (index < 0 || index >= static_cast<int>(m_discoveredNormalTrees.size())) {
        return false;
    }
    m_normalTreeManual = false;
    m_discoveredNormalTreeIndex = index;
    applyNormalTreeHighlight(m_discoveredNormalTrees[static_cast<size_t>(index)]);
    return true;
}

QString GraphScene::discoveredNormalTreeListLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(m_discoveredNormalTrees.size())) {
        return {};
    }
    const lg::NormalTreeResult& tree = m_discoveredNormalTrees[static_cast<size_t>(index)];
    QStringList stateVarText;
    stateVarText.reserve(static_cast<int>(tree.stateVariables.size()));
    for (const lg::NormalTreeResult::StateVariable& state : tree.stateVariables) {
        stateVarText.push_back(state.symbol);
    }
    const QString stateVarSummary =
        stateVarText.isEmpty() ? tr("none") : stateVarText.join(QStringLiteral(", "));
    const QString activeMark =
        index == m_discoveredNormalTreeIndex ? QStringLiteral(" *") : QString();
    return tr("Tree %1 — order %2: %3%4")
        .arg(index + 1)
        .arg(tree.stateVariables.size())
        .arg(stateVarSummary)
        .arg(activeMark);
}

lg::NormalTreeResult GraphScene::commitNormalTreeSelection(
    const std::vector<BranchItem*>& treeBranches) {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    collectGraphItems(nodes, branches, twoPorts);

    lg::NormalTreeResult result =
        lg::validateManualNormalTree(nodes, branches, twoPorts, treeBranches);
    if (result.status != lg::NormalTreeResult::Status::Ok) {
        return result;
    }

    const std::unordered_set<BranchItem*> inTree(treeBranches.begin(), treeBranches.end());
    for (BranchItem* branch : branches) {
        if (branch) {
            branch->setNormalTreeRole(inTree.count(branch) != 0, true);
        }
    }
    m_normalTreeHighlightActive = true;
    m_lastNormalTreeResult = result;
    m_lastStateSpaceResult = {};
    syncActiveSavedNormalTreeIndex();
    emit normalTreeHighlightChanged();
    return result;
}

BranchItem* GraphScene::branchBySerialId(int serialId) const {
    if (serialId <= 0) {
        return nullptr;
    }
    for (QGraphicsItem* item : items()) {
        if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            if (branch->serialId() == serialId) {
                return branch;
            }
        }
    }
    return nullptr;
}

std::vector<int> GraphScene::currentTreeBranchSerialIds() const {
    std::vector<int> ids;
    if (!m_normalTreeHighlightActive ||
        m_lastNormalTreeResult.status != lg::NormalTreeResult::Status::Ok) {
        return ids;
    }
    ids.reserve(m_lastNormalTreeResult.treeBranches.size());
    for (BranchItem* branch : m_lastNormalTreeResult.treeBranches) {
        if (branch && branch->serialId() > 0) {
            ids.push_back(branch->serialId());
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void GraphScene::syncActiveSavedNormalTreeIndex() {
    const std::vector<int> currentIds = currentTreeBranchSerialIds();
    if (currentIds.empty()) {
        m_activeSavedNormalTreeIndex = -1;
        return;
    }
    for (int i = 0; i < static_cast<int>(m_savedNormalTrees.size()); ++i) {
        std::vector<int> savedIds = m_savedNormalTrees[static_cast<size_t>(i)].treeBranchSerialIds;
        std::sort(savedIds.begin(), savedIds.end());
        if (savedIds == currentIds) {
            m_activeSavedNormalTreeIndex = i;
            return;
        }
    }
    m_activeSavedNormalTreeIndex = -1;
}

QString GraphScene::savedNormalTreeListLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(m_savedNormalTrees.size())) {
        return {};
    }
    const SavedNormalTree& saved = m_savedNormalTrees[static_cast<size_t>(index)];
    std::vector<BranchItem*> treeBranches;
    treeBranches.reserve(saved.treeBranchSerialIds.size());
    for (int serialId : saved.treeBranchSerialIds) {
        if (BranchItem* branch = branchBySerialId(serialId)) {
            treeBranches.push_back(branch);
        }
    }

    QString detail;
    if (treeBranches.size() != saved.treeBranchSerialIds.size()) {
        detail = tr("missing branches");
    } else {
        std::vector<NodeItem*> nodes;
        std::vector<BranchItem*> branches;
        std::vector<TwoPortItem*> twoPorts;
        collectGraphItems(nodes, branches, twoPorts);
        const lg::NormalTreeResult preview =
            lg::validateManualNormalTree(nodes, branches, twoPorts, treeBranches);
        if (preview.status == lg::NormalTreeResult::Status::Ok) {
            detail = tr("order %1, %2 branches")
                         .arg(preview.stateVariables.size())
                         .arg(preview.treeBranches.size());
        } else {
            detail = preview.message.isEmpty() ? tr("invalid") : preview.message;
        }
    }

    const QString activeMark = index == m_activeSavedNormalTreeIndex ? QStringLiteral(" *") : QString();
    return tr("%1 — %2%3").arg(saved.name, detail, activeMark);
}

bool GraphScene::addSavedNormalTree(const QString& name) {
    if (!m_normalTreeHighlightActive ||
        m_lastNormalTreeResult.status != lg::NormalTreeResult::Status::Ok) {
        return false;
    }
    const std::vector<int> ids = currentTreeBranchSerialIds();
    if (ids.empty()) {
        return false;
    }

    const std::vector<SavedNormalTree> before = m_savedNormalTrees;
    const int beforeActive = m_activeSavedNormalTreeIndex;

    SavedNormalTree saved;
    saved.name = name.trimmed().isEmpty() ? tr("Tree %1").arg(m_savedNormalTrees.size() + 1) : name.trimmed();
    saved.treeBranchSerialIds = ids;

    std::vector<SavedNormalTree> after = m_savedNormalTrees;
    after.push_back(saved);
    const int afterActive = static_cast<int>(after.size()) - 1;

    pushSavedNormalTreesUndo(before, beforeActive, after, afterActive);
    syncActiveSavedNormalTreeIndex();
    return true;
}

bool GraphScene::removeSavedNormalTree(int index) {
    if (index < 0 || index >= static_cast<int>(m_savedNormalTrees.size())) {
        return false;
    }

    const std::vector<SavedNormalTree> before = m_savedNormalTrees;
    const int beforeActive = m_activeSavedNormalTreeIndex;

    std::vector<SavedNormalTree> after = m_savedNormalTrees;
    after.erase(after.begin() + index);

    int afterActive = beforeActive;
    if (beforeActive == index) {
        afterActive = -1;
    } else if (beforeActive > index) {
        --afterActive;
    }

    pushSavedNormalTreesUndo(before, beforeActive, after, afterActive);
    syncActiveSavedNormalTreeIndex();
    return true;
}

bool GraphScene::applySavedNormalTree(int index) {
    if (index < 0 || index >= static_cast<int>(m_savedNormalTrees.size())) {
        return false;
    }
    const SavedNormalTree& saved = m_savedNormalTrees[static_cast<size_t>(index)];

    std::vector<BranchItem*> treeBranches;
    treeBranches.reserve(saved.treeBranchSerialIds.size());
    for (int serialId : saved.treeBranchSerialIds) {
        if (BranchItem* branch = branchBySerialId(serialId)) {
            treeBranches.push_back(branch);
        }
    }
    if (treeBranches.size() != saved.treeBranchSerialIds.size()) {
        return false;
    }

    const lg::NormalTreeResult result = commitNormalTreeSelection(treeBranches);
    if (result.status != lg::NormalTreeResult::Status::Ok) {
        return false;
    }
    m_normalTreeManual = true;
    m_activeSavedNormalTreeIndex = index;
    emit savedNormalTreesChanged();
    return true;
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
