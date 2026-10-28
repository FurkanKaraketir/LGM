#include "canvas.h"

#include "elemental_equation.h"
#include "normal_tree.h"

#include <QMessageBox>
#include <QUndoCommand>
#include <algorithm>
#include <vector>

namespace {

enum UndoMergeId {
    MoveNodeId = 1,
    MoveTwoPortId = 2,
    SetNodeNameId = 3,
    SetNodeAcrossId = 4,
    SetBranchNameId = 5,
    SetTwoPortNameId = 6,
    SetBranchConstantId = 7,
    SetTwoPortModulusId = 8,
};

// Commands refer to items by serial id, never by pointer: delete + undo (and merge + undo) re-create
// items as new objects carrying the same id, so a stored pointer would dangle.
template <typename Item>
class ItemRef {
public:
    ItemRef(GraphScene* scene, const Item* item) : m_scene(scene), m_id(item ? item->serialId() : 0) {}

    Item* get() const;

    bool operator==(const ItemRef& other) const { return m_id == other.m_id; }

    bool operator!=(const ItemRef& other) const { return m_id != other.m_id; }

private:
    GraphScene* m_scene;
    int m_id;
};

template <>
NodeItem* ItemRef<NodeItem>::get() const {
    return m_scene->nodeById(m_id);
}

template <>
BranchItem* ItemRef<BranchItem>::get() const {
    return m_scene->branchBySerialId(m_id);
}

template <>
TwoPortItem* ItemRef<TwoPortItem>::get() const {
    return m_scene->twoPortById(m_id);
}

class AddNodeCommand : public QUndoCommand {
public:
    AddNodeCommand(GraphScene* scene, const QPointF& pos) : m_scene(scene) {
        m_snap.pos = scene->snap(pos);
        setText("Add node");
    }

    void undo() override {
        if (NodeItem* node = m_scene->nodeById(m_snap.id)) {
            m_snap = GraphScene::snapshotOf(node);
            m_scene->destroyNode(node);
        }
    }

    void redo() override {
        NodeItem* node =
            m_snap.id == 0 ? m_scene->createNode(m_snap.pos) : m_scene->createNodeFromSnapshot(m_snap);
        if (node) {
            m_snap.id = node->serialId();
            m_scene->clearSelection();
            node->setSelected(true);
        }
    }

private:
    GraphScene* m_scene;
    GraphScene::NodeSnapshot m_snap;
};

class AddBranchCommand : public QUndoCommand {
public:
    AddBranchCommand(GraphScene* scene, NodeItem* a, NodeItem* b) : m_scene(scene) {
        m_snap.fromId = a->serialId();
        m_snap.toId = b->serialId();
        setText("Add branch");
    }

    void undo() override {
        if (BranchItem* branch = m_scene->branchBySerialId(m_snap.serialId)) {
            m_snap = GraphScene::snapshotOf(branch);
            m_scene->destroyBranch(branch);
        }
    }

    void redo() override {
        BranchItem* branch = nullptr;
        if (m_snap.serialId == 0) {
            NodeItem* a = m_scene->nodeById(m_snap.fromId);
            NodeItem* b = m_scene->nodeById(m_snap.toId);
            branch = a && b ? m_scene->createBranch(a, b) : nullptr;
            if (branch) {
                m_snap.serialId = branch->serialId();
            }
        } else {
            branch = m_scene->createBranchFromSnapshot(m_snap);
        }
        if (branch) {
            m_scene->clearSelection();
            branch->setSelected(true);
        }
    }

private:
    GraphScene* m_scene;
    GraphScene::BranchSnapshot m_snap;
};

class AddTwoPortCommand : public QUndoCommand {
public:
    AddTwoPortCommand(GraphScene* scene, const QPointF& center, TwoPortKind kind)
        : m_scene(scene), m_center(center), m_kind(kind) {
        setText("Add two-port");
    }

    void undo() override {
        if (TwoPortItem* item = m_scene->twoPortById(m_snap.key.id)) {
            m_snap = GraphScene::snapshotOf(item);
            m_scene->purgeTwoPort(item);
        }
    }

    void redo() override {
        TwoPortItem* item = m_snap.key.id == 0 ? m_scene->createTwoPort(m_center, m_kind)
                                               : m_scene->restoreTwoPortSnapshot(m_snap);
        if (item) {
            m_snap.key.id = item->serialId();
            m_scene->selectTwoPort(item);
        }
    }

private:
    GraphScene* m_scene;
    QPointF m_center;
    TwoPortKind m_kind;
    GraphScene::TwoPortSnapshot m_snap;
};

class FlipBranchCommand : public QUndoCommand {
public:
    FlipBranchCommand(GraphScene* scene, BranchItem* branch) : m_scene(scene), m_branch(scene, branch) {
        setText("Flip branch");
    }

    void undo() override { apply(); }

    void redo() override { apply(); }

private:
    void apply() {
        if (BranchItem* branch = m_branch.get()) {
            m_scene->flipBranch(branch);
        }
    }

    GraphScene* m_scene;
    ItemRef<BranchItem> m_branch;
};

class SetNodeNameCommand : public QUndoCommand {
public:
    SetNodeNameCommand(GraphScene* scene, NodeItem* node, const QString& oldName, const QString& newName)
        : m_node(scene, node), m_old(oldName), m_new(newName) {
        setText("Rename node");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* cmd = dynamic_cast<const SetNodeNameCommand*>(other);
        if (!cmd || cmd->m_node != m_node) {
            return false;
        }
        m_new = cmd->m_new;
        return true;
    }

    int id() const override { return SetNodeNameId; }

private:
    void apply(const QString& name) {
        if (NodeItem* node = m_node.get()) {
            node->setName(name);
        }
    }

    ItemRef<NodeItem> m_node;
    QString m_old;
    QString m_new;
};

class SetNodeAcrossVariableCommand : public QUndoCommand {
public:
    SetNodeAcrossVariableCommand(GraphScene* scene, NodeItem* node, const QString& oldSymbol,
                                 const QString& newSymbol)
        : m_scene(scene), m_node(scene, node), m_old(oldSymbol), m_new(newSymbol) {
        setText("Change node across variable");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* cmd = dynamic_cast<const SetNodeAcrossVariableCommand*>(other);
        if (!cmd || cmd->m_node != m_node) {
            return false;
        }
        m_new = cmd->m_new;
        return true;
    }

    int id() const override { return SetNodeAcrossId; }

private:
    void apply(const QString& symbol) {
        NodeItem* node = m_node.get();
        if (!node) {
            return;
        }
        const QString previous = node->acrossVariable();
        if (previous == symbol) {
            return;
        }
        node->setAcrossVariable(symbol);
        m_scene->renameOutputVariableSymbol(previous, symbol);
    }

    GraphScene* m_scene;
    ItemRef<NodeItem> m_node;
    QString m_old;
    QString m_new;
};

class SetNodeGroundCommand : public QUndoCommand {
public:
    SetNodeGroundCommand(GraphScene* scene, NodeItem* node, bool oldGround, bool newGround)
        : m_node(scene, node), m_old(oldGround), m_new(newGround) {
        setText("Change node type");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(bool ground) {
        if (NodeItem* node = m_node.get()) {
            node->setGround(ground);
        }
    }

    ItemRef<NodeItem> m_node;
    bool m_old;
    bool m_new;
};

class NodePropertiesCommand : public QUndoCommand {
public:
    NodePropertiesCommand(GraphScene* scene, NodeItem* node, bool oldGround, bool newGround,
                          SystemType oldType, SystemType newType, SystemType oldDefault,
                          SystemType newDefault)
        : m_scene(scene),
          m_node(scene, node),
          m_oldGround(oldGround),
          m_newGround(newGround),
          m_oldType(oldType),
          m_newType(newType),
          m_oldDefault(oldDefault),
          m_newDefault(newDefault) {
        setText("Change node properties");
    }

    void undo() override { apply(m_oldGround, m_oldType, m_oldDefault); }

    void redo() override { apply(m_newGround, m_newType, m_newDefault); }

private:
    void apply(bool ground, SystemType type, SystemType defaultType) {
        NodeItem* node = m_node.get();
        if (!node) {
            return;
        }
        node->setGround(ground);
        if (!ground) {
            node->setSystemType(type);
        }
        m_scene->setDefaultSystemType(defaultType);
    }

    GraphScene* m_scene;
    ItemRef<NodeItem> m_node;
    bool m_oldGround;
    bool m_newGround;
    SystemType m_oldType;
    SystemType m_newType;
    SystemType m_oldDefault;
    SystemType m_newDefault;
};

class SetBranchNameCommand : public QUndoCommand {
public:
    SetBranchNameCommand(GraphScene* scene, BranchItem* branch, const QString& oldName,
                         const QString& newName)
        : m_branch(scene, branch), m_old(oldName), m_new(newName) {
        setText("Rename branch");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* cmd = dynamic_cast<const SetBranchNameCommand*>(other);
        if (!cmd || cmd->m_branch != m_branch) {
            return false;
        }
        m_new = cmd->m_new;
        return true;
    }

    int id() const override { return SetBranchNameId; }

private:
    void apply(const QString& name) {
        if (BranchItem* branch = m_branch.get()) {
            branch->setName(name);
        }
    }

    ItemRef<BranchItem> m_branch;
    QString m_old;
    QString m_new;
};

class SetBranchActiveCommand : public QUndoCommand {
public:
    SetBranchActiveCommand(GraphScene* scene, BranchItem* branch, bool oldActive, bool newActive)
        : m_branch(scene, branch), m_old(oldActive), m_new(newActive) {
        setText("Change branch category");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(bool active) {
        if (BranchItem* branch = m_branch.get()) {
            branch->setActive(active);
        }
    }

    ItemRef<BranchItem> m_branch;
    bool m_old;
    bool m_new;
};

class SetBranchTypeCommand : public QUndoCommand {
public:
    SetBranchTypeCommand(GraphScene* scene, BranchItem* branch, BranchType oldType, BranchType newType)
        : m_branch(scene, branch), m_old(oldType), m_new(newType) {
        setText("Change branch element");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(BranchType type) {
        if (BranchItem* branch = m_branch.get()) {
            branch->setBranchType(type);
        }
    }

    ItemRef<BranchItem> m_branch;
    BranchType m_old;
    BranchType m_new;
};

class SetNodeSystemTypeCommand : public QUndoCommand {
public:
    SetNodeSystemTypeCommand(GraphScene* scene, NodeItem* node, SystemType oldType, SystemType newType,
                             SystemType oldDefault, SystemType newDefault)
        : m_scene(scene),
          m_node(scene, node),
          m_old(oldType),
          m_new(newType),
          m_oldDefault(oldDefault),
          m_newDefault(newDefault) {
        setText("Change node system type");
    }

    void undo() override { apply(m_old, m_oldDefault); }

    void redo() override { apply(m_new, m_newDefault); }

private:
    void apply(SystemType type, SystemType defaultType) {
        if (NodeItem* node = m_node.get()) {
            node->setSystemType(type);
        }
        m_scene->setDefaultSystemType(defaultType);
    }

    GraphScene* m_scene;
    ItemRef<NodeItem> m_node;
    SystemType m_old;
    SystemType m_new;
    SystemType m_oldDefault;
    SystemType m_newDefault;
};

class SetBranchConstantCommand : public QUndoCommand {
public:
    SetBranchConstantCommand(GraphScene* scene, BranchItem* branch, const QString& oldConstant,
                             const QString& newConstant)
        : m_branch(scene, branch), m_old(oldConstant), m_new(newConstant) {
        setText("Change branch constant");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* cmd = dynamic_cast<const SetBranchConstantCommand*>(other);
        if (!cmd || cmd->m_branch != m_branch) {
            return false;
        }
        m_new = cmd->m_new;
        return true;
    }

    int id() const override { return SetBranchConstantId; }

private:
    void apply(const QString& constant) {
        if (BranchItem* branch = m_branch.get()) {
            branch->setElementConstant(constant);
            lg::applyConstantThroughNaming(branch);
        }
    }

    ItemRef<BranchItem> m_branch;
    QString m_old;
    QString m_new;
};

class BranchPropertiesCommand : public QUndoCommand {
public:
    BranchPropertiesCommand(GraphScene* scene, BranchItem* branch, bool oldActive, BranchType oldType,
                            const QString& oldConstant, bool newActive, BranchType newType,
                            const QString& newConstant)
        : m_branch(scene, branch),
          m_oldActive(oldActive),
          m_oldType(oldType),
          m_oldConstant(oldConstant),
          m_newActive(newActive),
          m_newType(newType),
          m_newConstant(newConstant) {
        setText("Change branch properties");
    }

    void undo() override { apply(m_oldActive, m_oldType, m_oldConstant); }

    void redo() override { apply(m_newActive, m_newType, m_newConstant); }

private:
    void apply(bool active, BranchType type, const QString& constant) {
        BranchItem* branch = m_branch.get();
        if (!branch) {
            return;
        }
        branch->setActive(active);
        branch->setBranchType(type);
        if (!active) {
            branch->setElementConstant(constant);
        }
    }

    ItemRef<BranchItem> m_branch;
    bool m_oldActive;
    BranchType m_oldType;
    QString m_oldConstant;
    bool m_newActive;
    BranchType m_newType;
    QString m_newConstant;
};

class SetTwoPortNameCommand : public QUndoCommand {
public:
    SetTwoPortNameCommand(GraphScene* scene, TwoPortItem* item, const QString& oldName,
                          const QString& newName)
        : m_item(scene, item), m_old(oldName), m_new(newName) {
        setText("Rename two-port");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* cmd = dynamic_cast<const SetTwoPortNameCommand*>(other);
        if (!cmd || cmd->m_item != m_item) {
            return false;
        }
        m_new = cmd->m_new;
        return true;
    }

    int id() const override { return SetTwoPortNameId; }

private:
    void apply(const QString& name) {
        if (TwoPortItem* item = m_item.get()) {
            item->setName(name);
        }
    }

    ItemRef<TwoPortItem> m_item;
    QString m_old;
    QString m_new;
};

class SetTwoPortKindCommand : public QUndoCommand {
public:
    SetTwoPortKindCommand(GraphScene* scene, TwoPortItem* item, TwoPortKind from, TwoPortKind to)
        : m_item(scene, item), m_from(from), m_to(to) {
        setText("Change two-port type");
    }

    void undo() override { apply(m_from); }

    void redo() override { apply(m_to); }

private:
    void apply(TwoPortKind kind) {
        TwoPortItem* item = m_item.get();
        if (!item) {
            return;
        }
        if (item->modulus() == lg::defaultTwoPortModulus(item->kind())) {
            item->setModulus(lg::defaultTwoPortModulus(kind));
        }
        item->setKind(kind);
    }

    ItemRef<TwoPortItem> m_item;
    TwoPortKind m_from;
    TwoPortKind m_to;
};

class SetTwoPortModulusCommand : public QUndoCommand {
public:
    SetTwoPortModulusCommand(GraphScene* scene, TwoPortItem* item, const QString& oldModulus,
                             const QString& newModulus)
        : m_item(scene, item), m_old(oldModulus), m_new(newModulus) {
        setText("Change two-port modulus");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* cmd = dynamic_cast<const SetTwoPortModulusCommand*>(other);
        if (!cmd || cmd->m_item != m_item) {
            return false;
        }
        m_new = cmd->m_new;
        return true;
    }

    int id() const override { return SetTwoPortModulusId; }

private:
    void apply(const QString& modulus) {
        if (TwoPortItem* item = m_item.get()) {
            item->setModulus(modulus);
        }
    }

    ItemRef<TwoPortItem> m_item;
    QString m_old;
    QString m_new;
};

class DeleteItemsCommand : public QUndoCommand {
public:
    DeleteItemsCommand(GraphScene* scene) : m_scene(scene) {
        setText("Delete");
        m_scene->captureDeleteState(m_nodes, m_branches, m_twoPorts);
    }

    void undo() override { m_scene->restoreDelete(m_nodes, m_branches, m_twoPorts); }

    void redo() override { m_scene->executeDelete(m_nodes, m_branches, m_twoPorts); }

private:
    GraphScene* m_scene;
    std::vector<GraphScene::NodeSnapshot> m_nodes;
    std::vector<GraphScene::BranchSnapshot> m_branches;
    std::vector<GraphScene::TwoPortSnapshot> m_twoPorts;
};

void applyNodePos(NodeItem* node, const QPointF& pos) {
    if (!node) {
        return;
    }
    node->setPos(pos);
    for (BranchItem* branch : node->branches()) {
        branch->updatePath();
    }
    node->refreshTwoPorts();
}

class MoveNodeCommand : public QUndoCommand {
public:
    MoveNodeCommand(GraphScene* scene, NodeItem* node, const QPointF& oldPos, const QPointF& newPos)
        : m_node(scene, node), m_oldPos(oldPos), m_newPos(newPos) {
        setText("Move node");
    }

    void undo() override { applyNodePos(m_node.get(), m_oldPos); }

    void redo() override { applyNodePos(m_node.get(), m_newPos); }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* cmd = dynamic_cast<const MoveNodeCommand*>(other);
        if (!cmd || cmd->m_node != m_node) {
            return false;
        }
        m_newPos = cmd->m_newPos;
        return true;
    }

    int id() const override { return MoveNodeId; }

private:
    ItemRef<NodeItem> m_node;
    QPointF m_oldPos;
    QPointF m_newPos;
};

class MoveTwoPortCommand : public QUndoCommand {
public:
    MoveTwoPortCommand(GraphScene* scene, TwoPortItem* item, const QPointF& oldCenter,
                       const QPointF& newCenter)
        : m_item(scene, item), m_oldCenter(oldCenter), m_newCenter(newCenter) {
        setText("Move two-port");
    }

    void undo() override { apply(m_oldCenter); }

    void redo() override { apply(m_newCenter); }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* cmd = dynamic_cast<const MoveTwoPortCommand*>(other);
        if (!cmd || cmd->m_item != m_item) {
            return false;
        }
        m_newCenter = cmd->m_newCenter;
        return true;
    }

    int id() const override { return MoveTwoPortId; }

private:
    void apply(const QPointF& targetCenter) {
        if (TwoPortItem* item = m_item.get()) {
            item->moveBy(targetCenter - item->center());
        }
    }

    ItemRef<TwoPortItem> m_item;
    QPointF m_oldCenter;
    QPointF m_newCenter;
};

TwoPortItem* sharedTwoPort(const NodeItem* a, const NodeItem* b) {
    for (TwoPortItem* twoPort : a->twoPorts()) {
        const auto& others = b->twoPorts();
        if (std::find(others.begin(), others.end(), twoPort) != others.end()) {
            return twoPort;
        }
    }
    return nullptr;
}

bool isReferencePort(const NodeItem* node) {
    for (const TwoPortItem* twoPort : node->twoPorts()) {
        if (node == twoPort->g1() || node == twoPort->g2()) {
            return true;
        }
    }
    return false;
}

struct MergeEndpoints {
    NodeItem* keep = nullptr;
    NodeItem* remove = nullptr;
};

MergeEndpoints resolveMergeKeepRemove(NodeItem* a, NodeItem* b) {
    if (TwoPortItem* shared = sharedTwoPort(a, b)) {
        // Collapsing a two-port's references: the g1 node survives.
        return b == shared->g1() ? MergeEndpoints{b, a} : MergeEndpoints{a, b};
    }
    const size_t ownersA = a->twoPorts().size();
    const size_t ownersB = b->twoPorts().size();
    // Keep the node that already belongs to more two-ports so a cascade junction keeps its identity.
    return ownersB > ownersA ? MergeEndpoints{b, a} : MergeEndpoints{a, b};
}

class MergeNodesCommand : public QUndoCommand {
public:
    MergeNodesCommand(GraphScene* scene, NodeItem* keep, NodeItem* remove, NodeItem* dragged,
                      const QPointF& draggedOldPos)
        : m_scene(scene),
          m_keepId(keep->serialId()),
          m_removeId(remove->serialId()),
          m_keepWasDragged(dragged && dragged == keep),
          m_removeWasDragged(dragged && dragged == remove),
          m_draggedOldPos(draggedOldPos),
          m_keepDropPos(keep->scenePos()) {
        setText("Combine nodes");
    }

    void undo() override {
        NodeItem* keep = m_scene->nodeById(m_keepId);
        if (!keep) {
            return;
        }
        m_scene->unmergeNodes(keep, m_undo);
        if (m_keepWasDragged) {
            applyNodePos(keep, m_draggedOldPos);
        }
    }

    void redo() override {
        NodeItem* keep = m_scene->nodeById(m_keepId);
        NodeItem* remove = m_scene->nodeById(m_removeId);
        if (!keep || !remove || keep == remove) {
            return;
        }
        if (m_keepWasDragged) {
            applyNodePos(keep, m_keepDropPos);
        }
        m_scene->mergeNodes(keep, remove, &m_undo);
        if (m_removeWasDragged) {
            // Undo puts the dragged node back where the drag started, not on top of `keep`.
            m_undo.remove.pos = m_draggedOldPos;
        }
    }

private:
    GraphScene* m_scene;
    int m_keepId;
    int m_removeId;
    GraphScene::MergeUndoData m_undo;
    bool m_keepWasDragged;
    bool m_removeWasDragged;
    QPointF m_draggedOldPos;
    QPointF m_keepDropPos;
};

class SavedNormalTreesCommand : public QUndoCommand {
public:
    SavedNormalTreesCommand(GraphScene* scene, std::vector<GraphScene::SavedNormalTree> before,
                            int beforeActive, std::vector<GraphScene::SavedNormalTree> after,
                            int afterActive)
        : m_scene(scene),
          m_before(std::move(before)),
          m_after(std::move(after)),
          m_beforeActive(beforeActive),
          m_afterActive(afterActive) {
        setText("Edit saved normal trees");
    }

    void undo() override { m_scene->setSavedNormalTreesState(m_before, m_beforeActive); }

    void redo() override { m_scene->setSavedNormalTreesState(m_after, m_afterActive); }

private:
    GraphScene* m_scene = nullptr;
    std::vector<GraphScene::SavedNormalTree> m_before;
    std::vector<GraphScene::SavedNormalTree> m_after;
    int m_beforeActive = -1;
    int m_afterActive = -1;
};

}  // namespace

void GraphScene::pushAddNode(const QPointF& center) {
    m_undoStack.push(new AddNodeCommand(this, center));
}

void GraphScene::pushConnectNodes(NodeItem* a, NodeItem* b) {
    m_undoStack.push(new AddBranchCommand(this, a, b));
}

void GraphScene::pushFlipBranch(BranchItem* branch) {
    if (!branch) {
        return;
    }
    m_undoStack.push(new FlipBranchCommand(this, branch));
}

void GraphScene::pushAddTwoPort(const QPointF& center, TwoPortKind kind) {
    m_undoStack.push(new AddTwoPortCommand(this, snap(center), kind));
}

void GraphScene::pushToggleTwoPortKind(TwoPortItem* item) {
    if (!item) {
        return;
    }
    const TwoPortKind from = item->kind();
    const TwoPortKind to =
        from == TwoPortKind::Transformer ? TwoPortKind::Gyrator : TwoPortKind::Transformer;
    pushSetTwoPortKind(item, to);
}

void GraphScene::pushSetNodeName(NodeItem* node, const QString& name) {
    if (!node || node->name() == name) {
        return;
    }
    m_undoStack.push(new SetNodeNameCommand(this, node, node->name(), name));
}

void GraphScene::pushSetNodeAcrossVariable(NodeItem* node, const QString& symbol) {
    if (!node || node->isGround() || node->acrossVariable() == symbol) {
        return;
    }
    m_undoStack.push(new SetNodeAcrossVariableCommand(this, node, node->acrossVariable(), symbol));
}

void GraphScene::pushSetNodeGround(NodeItem* node, bool ground) {
    if (!node || node->isGround() == ground) {
        return;
    }
    m_undoStack.push(new SetNodeGroundCommand(this, node, node->isGround(), ground));
}

void GraphScene::pushNodeProperties(NodeItem* node, bool oldGround, bool newGround, SystemType oldType,
                                    SystemType newType) {
    if (!node) {
        return;
    }
    if (oldGround == newGround && (oldGround || oldType == newType)) {
        return;
    }
    const SystemType oldDefault = m_defaultSystemType;
    const SystemType newDefault = !newGround ? newType : oldDefault;
    if (!newGround) {
        m_defaultSystemType = newType;
    }
    m_undoStack.push(
        new NodePropertiesCommand(this, node, oldGround, newGround, oldType, newType, oldDefault, newDefault));
}

void GraphScene::pushSetBranchName(BranchItem* branch, const QString& name) {
    if (!branch || branch->name() == name) {
        return;
    }
    m_undoStack.push(new SetBranchNameCommand(this, branch, branch->name(), name));
}

void GraphScene::pushSetBranchActive(BranchItem* branch, bool active) {
    if (!branch || branch->isActive() == active) {
        return;
    }
    m_undoStack.push(new SetBranchActiveCommand(this, branch, branch->isActive(), active));
}

void GraphScene::pushSetBranchType(BranchItem* branch, BranchType type) {
    if (!branch || branch->branchType() == type) {
        return;
    }
    m_undoStack.push(new SetBranchTypeCommand(this, branch, branch->branchType(), type));
}

void GraphScene::pushSetNodeSystemType(NodeItem* node, SystemType type) {
    if (!node || node->isGround() || node->systemType() == type) {
        return;
    }
    const SystemType oldDefault = m_defaultSystemType;
    m_defaultSystemType = type;
    m_undoStack.push(
        new SetNodeSystemTypeCommand(this, node, node->systemType(), type, oldDefault, type));
}

void GraphScene::pushSetBranchConstant(BranchItem* branch, const QString& constant) {
    if (!branch || branch->elementConstant() == constant) {
        return;
    }
    if (!lg::isValidElementConstant(constant)) {
        return;
    }
    m_undoStack.push(new SetBranchConstantCommand(this, branch, branch->elementConstant(), constant));
}

void GraphScene::pushBranchProperties(BranchItem* branch, bool active, BranchType type, const QString& constant) {
    if (!branch) {
        return;
    }
    const bool oldActive = branch->isActive();
    const BranchType oldType = branch->branchType();
    const QString oldConstant = branch->elementConstant();
    if (oldActive == active && oldType == type && oldConstant == constant) {
        return;
    }
    if (!active && !lg::isValidElementConstant(constant)) {
        return;
    }
    m_undoStack.push(
        new BranchPropertiesCommand(this, branch, oldActive, oldType, oldConstant, active, type, constant));
}

void GraphScene::pushSetTwoPortName(TwoPortItem* item, const QString& name) {
    if (!item || item->name() == name) {
        return;
    }
    m_undoStack.push(new SetTwoPortNameCommand(this, item, item->name(), name));
}

void GraphScene::pushSetTwoPortKind(TwoPortItem* item, TwoPortKind kind) {
    if (!item || item->kind() == kind) {
        return;
    }
    m_undoStack.push(new SetTwoPortKindCommand(this, item, item->kind(), kind));
}

void GraphScene::pushSetTwoPortModulus(TwoPortItem* item, const QString& modulus) {
    if (!item || item->modulus() == modulus) {
        return;
    }
    m_undoStack.push(new SetTwoPortModulusCommand(this, item, item->modulus(), modulus));
}

void GraphScene::pushMove(NodeItem* node, const QPointF& oldPos, const QPointF& newPos) {
    const QPointF from = snap(oldPos);
    const QPointF to = snap(newPos);
    if (!node || from == to) {
        return;
    }
    m_undoStack.push(new MoveNodeCommand(this, node, from, to));
}

void GraphScene::pushMoveTwoPort(TwoPortItem* item, const QPointF& oldCenter, const QPointF& newCenter) {
    if (!item || oldCenter == newCenter) {
        return;
    }
    m_undoStack.push(new MoveTwoPortCommand(this, item, oldCenter, newCenter));
}

bool GraphScene::pushMergeNodes(NodeItem* a, NodeItem* b, NodeItem* dragged,
                                const QPointF& draggedOldPos) {
    QString reason;
    if (!canMergeNodes(a, b, &reason)) {
        if (!views().isEmpty()) {
            QMessageBox::warning(views().first(), tr("Cannot combine nodes"), reason);
        }
        return false;
    }

    const MergeEndpoints endpoints = resolveMergeKeepRemove(a, b);
    NodeItem* keep = endpoints.keep;
    NodeItem* remove = endpoints.remove;
    TwoPortItem* keepTp = keep->twoPort();
    TwoPortItem* removeTp = remove->twoPort();

    QString warning;
    if (TwoPortItem* shared = sharedTwoPort(keep, remove)) {
        warning = tr("The two reference nodes of \"%1\" will share one node (3-terminal two-port).\n"
                     "External connections on \"%2\" move to the kept reference.\n\nContinue?")
                      .arg(shared->name(), remove->name());
    } else if (keepTp && removeTp) {
        if (isReferencePort(keep)) {
            warning = tr("Cascade: reference nodes \"%1\" (%2) and \"%3\" (%4) become one shared "
                         "junction.\n\nContinue?")
                          .arg(remove->name(), removeTp->name(), keep->name(), keepTp->name());
        } else {
            warning = tr("Cascade: \"%1\" (%2) and \"%3\" (%4) merge into one across junction.\n"
                         "Both two-ports share this node.\n\nContinue?")
                          .arg(remove->name(), removeTp->name(), keep->name(), keepTp->name());
        }
    } else if (keepTp) {
        if (isReferencePort(keep)) {
            warning = tr("Reference \"%1\" will join \"%2\" (%3's reference).\n"
                         "Branches on \"%1\" attach to the kept node.\n\nContinue?")
                          .arg(remove->name(), keep->name(), keepTp->name());
        } else {
            warning = tr("Branches on \"%1\" will attach to \"%2\" (%3).\n\nContinue?")
                          .arg(remove->name(), keep->name(), keepTp->name());
        }
    } else {
        warning = tr("Branches on \"%1\" will move to \"%2\" and \"%1\" will be deleted.\n\nContinue?")
                      .arg(remove->name(), keep->name());
    }
    if (!keep->isGround() && !remove->isGround() && keep->systemType() != remove->systemType()) {
        warning += tr("\n\nNote: \"%1\" is %2 and \"%3\" is %4; the combined node stays %4.")
                       .arg(remove->name(), lg::systemTypeLabel(remove->systemType()), keep->name(),
                            lg::systemTypeLabel(keep->systemType()));
    }

    if (views().isEmpty()) {
        return false;
    }
    if (QMessageBox::warning(views().first(), tr("Combine nodes?"), warning, QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::No) != QMessageBox::Yes) {
        return false;
    }

    m_undoStack.push(new MergeNodesCommand(this, keep, remove, dragged, draggedOldPos));
    return true;
}

void GraphScene::pushDeleteSelection() {
    if (selectedItems().isEmpty()) {
        return;
    }
    clearBranchPending();
    m_undoStack.push(new DeleteItemsCommand(this));
}

void GraphScene::undo() {
    m_undoStack.undo();
}

void GraphScene::pushSavedNormalTreesUndo(const std::vector<SavedNormalTree>& before, int beforeActive,
                                          const std::vector<SavedNormalTree>& after, int afterActive) {
    m_undoStack.push(new SavedNormalTreesCommand(this, before, beforeActive, after, afterActive));
}

void GraphScene::setSavedNormalTreesState(const std::vector<SavedNormalTree>& trees, int activeIndex) {
    m_savedNormalTrees = trees;
    m_activeSavedNormalTreeIndex = activeIndex;
    emit savedNormalTreesChanged();
}

void GraphScene::redo() {
    m_undoStack.redo();
}
