#include "canvas.h"

#include "elemental_equation.h"

#include <QMessageBox>
#include <QUndoCommand>
#include <vector>

namespace {

BranchItem* findBranch(GraphScene* scene, const GraphScene::BranchKey& key) {
    NodeItem* a = scene->nodeAtPos(key.from);
    NodeItem* b = scene->nodeAtPos(key.to);
    if (!a || !b) {
        return nullptr;
    }
    for (BranchItem* branch : scene->branchesBetween(a, b)) {
        if (branch->index() == key.index) {
            return branch;
        }
    }
    return nullptr;
}

class AddNodeCommand : public QUndoCommand {
public:
    AddNodeCommand(GraphScene* scene, const QPointF& pos) : m_scene(scene), m_pos(pos) {
        setText("Add node");
    }

    void undo() override {
        if (!m_exists) {
            return;
        }
        if (NodeItem* node = m_scene->nodeAtPos(m_pos)) {
            m_scene->destroyNode(node);
        }
        m_exists = false;
    }

    void redo() override {
        if (m_exists) {
            return;
        }
        if (NodeItem* node = m_scene->createNode(m_pos)) {
            m_scene->clearSelection();
            node->setSelected(true);
        }
        m_exists = true;
    }

private:
    GraphScene* m_scene;
    QPointF m_pos;
    bool m_exists = false;
};

class AddBranchCommand : public QUndoCommand {
public:
    AddBranchCommand(GraphScene* scene, NodeItem* a, NodeItem* b)
        : m_scene(scene), m_key{a->scenePos(), b->scenePos(), 0} {
        setText("Add branch");
    }

    void undo() override {
        if (!m_exists) {
            return;
        }
        m_scene->destroyBranchAt(m_key);
        m_exists = false;
    }

    void redo() override {
        if (m_exists) {
            return;
        }
        NodeItem* a = m_scene->nodeAtPos(m_key.from);
        NodeItem* b = m_scene->nodeAtPos(m_key.to);
        if (!a || !b) {
            return;
        }
        if (BranchItem* branch = m_scene->createBranch(a, b)) {
            m_key.index = branch->index();
            m_scene->clearSelection();
            branch->setSelected(true);
        }
        m_exists = true;
    }

private:
    GraphScene* m_scene;
    GraphScene::BranchKey m_key;
    bool m_exists = false;
};

class AddTwoPortCommand : public QUndoCommand {
public:
    AddTwoPortCommand(GraphScene* scene, const QPointF& center, TwoPortKind kind)
        : m_scene(scene), m_key{center, kind} {
        setText("Add two-port");
    }

    void undo() override {
        if (!m_exists) {
            return;
        }
        if (TwoPortItem* item = m_scene->twoPortAtCenter(m_key.center)) {
            m_scene->destroyTwoPort(item);
        }
        m_exists = false;
    }

    void redo() override {
        if (m_exists) {
            return;
        }
        if (TwoPortItem* item = m_scene->createTwoPort(m_key.center, m_key.kind)) {
            m_scene->selectTwoPort(item);
        }
        m_exists = true;
    }

private:
    GraphScene* m_scene;
    GraphScene::TwoPortKey m_key;
    bool m_exists = false;
};

class FlipBranchCommand : public QUndoCommand {
public:
    FlipBranchCommand(GraphScene* scene, BranchItem* branch)
        : m_scene(scene),
          m_key{branch->from()->scenePos(), branch->to()->scenePos(), branch->index()} {
        setText("Flip branch");
    }

    void undo() override { apply(); }

    void redo() override { apply(); }

private:
    void apply() {
        if (BranchItem* branch = findBranch(m_scene, m_key)) {
            m_scene->flipBranch(branch);
        }
    }

    GraphScene* m_scene;
    GraphScene::BranchKey m_key;
};

class SetNodeNameCommand : public QUndoCommand {
public:
    SetNodeNameCommand(GraphScene* scene, const QPointF& pos, const QString& oldName, const QString& newName)
        : m_scene(scene), m_pos(pos), m_old(oldName), m_new(newName) {
        setText("Rename node");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(const QString& name) {
        if (NodeItem* node = m_scene->nodeAtPos(m_pos)) {
            node->setName(name);
        }
    }

    GraphScene* m_scene;
    QPointF m_pos;
    QString m_old;
    QString m_new;
};

class SetNodeAcrossVariableCommand : public QUndoCommand {
public:
    SetNodeAcrossVariableCommand(GraphScene* scene, const QPointF& pos, const QString& oldSymbol,
                                 const QString& newSymbol)
        : m_scene(scene), m_pos(pos), m_old(oldSymbol), m_new(newSymbol) {
        setText("Change node across variable");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(const QString& symbol) {
        if (NodeItem* node = m_scene->nodeAtPos(m_pos)) {
            node->setAcrossVariable(symbol);
        }
    }

    GraphScene* m_scene;
    QPointF m_pos;
    QString m_old;
    QString m_new;
};

class SetNodeGroundCommand : public QUndoCommand {
public:
    SetNodeGroundCommand(GraphScene* scene, const QPointF& pos, bool oldGround, bool newGround)
        : m_scene(scene), m_pos(pos), m_old(oldGround), m_new(newGround) {
        setText("Change node type");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(bool ground) {
        if (NodeItem* node = m_scene->nodeAtPos(m_pos)) {
            node->setGround(ground);
        }
    }

    GraphScene* m_scene;
    QPointF m_pos;
    bool m_old;
    bool m_new;
};

class SetBranchNameCommand : public QUndoCommand {
public:
    SetBranchNameCommand(GraphScene* scene, const GraphScene::BranchKey& key, const QString& oldName,
                         const QString& newName)
        : m_scene(scene), m_key(key), m_old(oldName), m_new(newName) {
        setText("Rename branch");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(const QString& name) {
        if (BranchItem* branch = findBranch(m_scene, m_key)) {
            branch->setName(name);
        }
    }

    GraphScene* m_scene;
    GraphScene::BranchKey m_key;
    QString m_old;
    QString m_new;
};

class SetBranchActiveCommand : public QUndoCommand {
public:
    SetBranchActiveCommand(GraphScene* scene, const GraphScene::BranchKey& key, bool oldActive, bool newActive)
        : m_scene(scene), m_key(key), m_old(oldActive), m_new(newActive) {
        setText("Change branch category");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(bool active) {
        if (BranchItem* branch = findBranch(m_scene, m_key)) {
            branch->setActive(active);
        }
    }

    GraphScene* m_scene;
    GraphScene::BranchKey m_key;
    bool m_old;
    bool m_new;
};

class SetBranchTypeCommand : public QUndoCommand {
public:
    SetBranchTypeCommand(GraphScene* scene, const GraphScene::BranchKey& key, BranchType oldType, BranchType newType)
        : m_scene(scene), m_key(key), m_old(oldType), m_new(newType) {
        setText("Change branch element");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(BranchType type) {
        if (BranchItem* branch = findBranch(m_scene, m_key)) {
            branch->setBranchType(type);
        }
    }

    GraphScene* m_scene;
    GraphScene::BranchKey m_key;
    BranchType m_old;
    BranchType m_new;
};

class SetBranchConstantCommand : public QUndoCommand {
public:
    SetBranchConstantCommand(GraphScene* scene, const GraphScene::BranchKey& key, const QString& oldConstant,
                             const QString& newConstant)
        : m_scene(scene), m_key(key), m_old(oldConstant), m_new(newConstant) {
        setText("Change branch constant");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(const QString& constant) {
        if (BranchItem* branch = findBranch(m_scene, m_key)) {
            branch->setElementConstant(constant);
        }
    }

    GraphScene* m_scene;
    GraphScene::BranchKey m_key;
    QString m_old;
    QString m_new;
};

class BranchPropertiesCommand : public QUndoCommand {
public:
    BranchPropertiesCommand(GraphScene* scene, const GraphScene::BranchKey& key, bool oldActive, BranchType oldType,
                            const QString& oldConstant, bool newActive, BranchType newType, const QString& newConstant)
        : m_scene(scene),
          m_key(key),
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
        if (BranchItem* branch = findBranch(m_scene, m_key)) {
            branch->setActive(active);
            branch->setBranchType(type);
            if (!active) {
                branch->setElementConstant(constant);
            }
        }
    }

    GraphScene* m_scene;
    GraphScene::BranchKey m_key;
    bool m_oldActive;
    BranchType m_oldType;
    QString m_oldConstant;
    bool m_newActive;
    BranchType m_newType;
    QString m_newConstant;
};

class SetTwoPortNameCommand : public QUndoCommand {
public:
    SetTwoPortNameCommand(GraphScene* scene, const QPointF& center, const QString& oldName, const QString& newName)
        : m_scene(scene), m_center(center), m_old(oldName), m_new(newName) {
        setText("Rename two-port");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(const QString& name) {
        if (TwoPortItem* item = m_scene->twoPortAtCenter(m_center)) {
            item->setName(name);
        }
    }

    GraphScene* m_scene;
    QPointF m_center;
    QString m_old;
    QString m_new;
};

class SetTwoPortKindCommand : public QUndoCommand {
public:
    SetTwoPortKindCommand(GraphScene* scene, const QPointF& center, TwoPortKind from, TwoPortKind to)
        : m_scene(scene), m_center(center), m_from(from), m_to(to) {
        setText("Change two-port type");
    }

    void undo() override { apply(m_from); }

    void redo() override { apply(m_to); }

private:
    void apply(TwoPortKind kind) {
        if (TwoPortItem* item = m_scene->twoPortAtCenter(m_center)) {
            if (item->modulus() == lg::defaultTwoPortModulus(item->kind())) {
                item->setModulus(lg::defaultTwoPortModulus(kind));
            }
            item->setKind(kind);
        }
    }

    GraphScene* m_scene;
    QPointF m_center;
    TwoPortKind m_from;
    TwoPortKind m_to;
};

class SetTwoPortModulusCommand : public QUndoCommand {
public:
    SetTwoPortModulusCommand(GraphScene* scene, const QPointF& center, const QString& oldModulus,
                             const QString& newModulus)
        : m_scene(scene), m_center(center), m_old(oldModulus), m_new(newModulus) {
        setText("Change two-port modulus");
    }

    void undo() override { apply(m_old); }

    void redo() override { apply(m_new); }

private:
    void apply(const QString& modulus) {
        if (TwoPortItem* item = m_scene->twoPortAtCenter(m_center)) {
            item->setModulus(modulus);
        }
    }

    GraphScene* m_scene;
    QPointF m_center;
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
    std::vector<QPointF> m_nodes;
    std::vector<GraphScene::BranchKey> m_branches;
    std::vector<GraphScene::TwoPortKey> m_twoPorts;
};

class MoveNodeCommand : public QUndoCommand {
public:
    MoveNodeCommand(GraphScene* scene, NodeItem* node, const QPointF& oldPos, const QPointF& newPos)
        : m_scene(scene), m_node(node), m_oldPos(oldPos), m_newPos(newPos) {
        setText("Move node");
    }

    void undo() override { apply(m_oldPos); }

    void redo() override { apply(m_newPos); }

    bool mergeWith(const QUndoCommand* other) override {
        const auto* cmd = dynamic_cast<const MoveNodeCommand*>(other);
        if (!cmd || cmd->m_node != m_node) {
            return false;
        }
        m_newPos = cmd->m_newPos;
        return true;
    }

    int id() const override { return 1; }

private:
    void apply(const QPointF& pos) {
        if (!m_node || !m_node->scene()) {
            return;
        }
        m_node->setPos(pos);
        for (BranchItem* branch : m_node->branches()) {
            branch->updatePath();
        }
        if (TwoPortItem* twoPort = m_node->twoPort()) {
            twoPort->refresh();
        }
    }

    GraphScene* m_scene;
    NodeItem* m_node;
    QPointF m_oldPos;
    QPointF m_newPos;
};

class MoveTwoPortCommand : public QUndoCommand {
public:
    MoveTwoPortCommand(GraphScene* scene, TwoPortItem* item, const QPointF& oldCenter,
                       const QPointF& newCenter)
        : m_scene(scene), m_center(m_scene->snap(item->center())), m_oldCenter(oldCenter),
          m_newCenter(newCenter) {
        setText("Move two-port");
    }

    void undo() override { apply(m_oldCenter); }

    void redo() override { apply(m_newCenter); }

private:
    void apply(const QPointF& targetCenter) {
        if (TwoPortItem* item = m_scene->twoPortAtCenter(m_center)) {
            item->moveBy(targetCenter - item->center());
        }
    }

    GraphScene* m_scene;
    QPointF m_center;
    QPointF m_oldCenter;
    QPointF m_newCenter;
};

class MergeNodesCommand : public QUndoCommand {
public:
    MergeNodesCommand(GraphScene* scene, NodeItem* keep, NodeItem* remove)
        : m_scene(scene), m_keep(keep), m_remove(remove) {
        setText("Combine nodes");
    }

    void undo() override {
        if (!m_done) {
            return;
        }
        m_scene->unmergeNodes(m_keep, m_undo);
        m_done = false;
    }

    void redo() override {
        NodeItem* remove = m_done ? m_scene->nodeAtPos(m_undo.removePos) : m_remove;
        if (!remove || remove == m_keep) {
            return;
        }
        if (m_done) {
            m_scene->mergeNodes(m_keep, remove, nullptr);
        } else {
            m_scene->mergeNodes(m_keep, remove, &m_undo);
            m_done = true;
        }
    }

private:
    GraphScene* m_scene;
    NodeItem* m_keep;
    NodeItem* m_remove;
    GraphScene::MergeUndoData m_undo;
    bool m_done = false;
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
    m_undoStack.push(new SetNodeNameCommand(this, node->scenePos(), node->name(), name));
}

void GraphScene::pushSetNodeAcrossVariable(NodeItem* node, const QString& symbol) {
    if (!node || node->isGround() || node->acrossVariable() == symbol) {
        return;
    }
    m_undoStack.push(
        new SetNodeAcrossVariableCommand(this, node->scenePos(), node->acrossVariable(), symbol));
}

void GraphScene::pushSetNodeGround(NodeItem* node, bool ground) {
    if (!node || node->isGround() == ground) {
        return;
    }
    m_undoStack.push(new SetNodeGroundCommand(this, node->scenePos(), node->isGround(), ground));
}

void GraphScene::pushSetBranchName(BranchItem* branch, const QString& name) {
    if (!branch || branch->name() == name) {
        return;
    }
    const BranchKey key{branch->from()->scenePos(), branch->to()->scenePos(), branch->index()};
    m_undoStack.push(new SetBranchNameCommand(this, key, branch->name(), name));
}

void GraphScene::pushSetBranchActive(BranchItem* branch, bool active) {
    if (!branch || branch->isActive() == active) {
        return;
    }
    const BranchKey key{branch->from()->scenePos(), branch->to()->scenePos(), branch->index()};
    m_undoStack.push(new SetBranchActiveCommand(this, key, branch->isActive(), active));
}

void GraphScene::pushSetBranchType(BranchItem* branch, BranchType type) {
    if (!branch || branch->branchType() == type) {
        return;
    }
    const BranchKey key{branch->from()->scenePos(), branch->to()->scenePos(), branch->index()};
    m_undoStack.push(new SetBranchTypeCommand(this, key, branch->branchType(), type));
}

void GraphScene::pushSetBranchConstant(BranchItem* branch, const QString& constant) {
    if (!branch || branch->elementConstant() == constant) {
        return;
    }
    const BranchKey key{branch->from()->scenePos(), branch->to()->scenePos(), branch->index()};
    m_undoStack.push(new SetBranchConstantCommand(this, key, branch->elementConstant(), constant));
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
    const BranchKey key{branch->from()->scenePos(), branch->to()->scenePos(), branch->index()};
    m_undoStack.push(
        new BranchPropertiesCommand(this, key, oldActive, oldType, oldConstant, active, type, constant));
}

void GraphScene::pushSetTwoPortName(TwoPortItem* item, const QString& name) {
    if (!item || item->name() == name) {
        return;
    }
    m_undoStack.push(new SetTwoPortNameCommand(this, item->center(), item->name(), name));
}

void GraphScene::pushSetTwoPortKind(TwoPortItem* item, TwoPortKind kind) {
    if (!item || item->kind() == kind) {
        return;
    }
    m_undoStack.push(new SetTwoPortKindCommand(this, item->center(), item->kind(), kind));
}

void GraphScene::pushSetTwoPortModulus(TwoPortItem* item, const QString& modulus) {
    if (!item || item->modulus() == modulus) {
        return;
    }
    m_undoStack.push(new SetTwoPortModulusCommand(this, item->center(), item->modulus(), modulus));
}

void GraphScene::pushMove(NodeItem* node, const QPointF& oldPos, const QPointF& newPos) {
    m_undoStack.push(new MoveNodeCommand(this, node, oldPos, newPos));
}

void GraphScene::pushMoveTwoPort(TwoPortItem* item, const QPointF& oldCenter, const QPointF& newCenter) {
    if (!item || oldCenter == newCenter) {
        return;
    }
    m_undoStack.push(new MoveTwoPortCommand(this, item, oldCenter, newCenter));
}

void GraphScene::pushMergeNodes(NodeItem* a, NodeItem* b) {
    QString reason;
    if (!canMergeNodes(a, b, &reason)) {
        if (!views().isEmpty()) {
            QMessageBox::warning(views().first(), tr("Cannot combine nodes"), reason);
        }
        return;
    }

    NodeItem* keep = a;
    NodeItem* remove = b;
    TwoPortItem* tpA = a->twoPort();
    TwoPortItem* tpB = b->twoPort();
    if (tpA && tpA == tpB) {
        keep = tpA->g1();
        remove = a == keep ? b : a;
    } else if (tpA && tpB && tpA != tpB) {
        const int roleA = a == tpA->v1() ? 1 : a == tpA->v2() ? 2 : a == tpA->g1() ? 3 : a == tpA->g2() ? 4 : 0;
        const int roleB = b == tpB->v1() ? 1 : b == tpB->v2() ? 2 : b == tpB->g1() ? 3 : b == tpB->g2() ? 4 : 0;
        const bool acrossA = roleA >= 1 && roleA <= 2;
        const bool acrossB = roleB >= 1 && roleB <= 2;
        if (acrossA && !acrossB) {
            keep = a;
            remove = b;
        } else if (acrossB && !acrossA) {
            keep = b;
            remove = a;
        }
    } else if (tpA && !tpB) {
        keep = a;
        remove = b;
    } else if (tpB && !tpA) {
        keep = b;
        remove = a;
    }

    QString warning;
    TwoPortItem* tp = keep->twoPort() ? keep->twoPort() : remove->twoPort();
    if (tpA && tpB && tpA == tpB) {
        warning = tr("The two reference nodes of \"%1\" will share one node (3-terminal two-port).\n"
                     "External connections on \"%2\" move to the kept reference.\n\nContinue?")
                      .arg(tp->name(), remove->name());
    } else if (tpA && tpB && tpA != tpB) {
        const bool refMerge = (a == tpA->g1() || a == tpA->g2()) && (b == tpB->g1() || b == tpB->g2());
        if (refMerge) {
            warning = tr("Cascade: reference nodes \"%1\" (%2) and \"%3\" (%4) become one shared "
                         "junction.\n\nContinue?")
                          .arg(remove->name(), tpB->name(), keep->name(), tpA->name());
        } else {
            warning = tr("Cascade: \"%1\" (%2) and \"%3\" (%4) merge into one across junction.\n"
                         "Both two-ports share this node.\n\nContinue?")
                          .arg(remove->name(), tpB->name(), keep->name(), tpA->name());
        }
    } else if (tp) {
        warning = tr("Branches on \"%1\" will attach to \"%2\" (%3).\n\nContinue?")
                      .arg(remove->name(), keep->name(), tp->name());
    } else {
        warning = tr("Branches on \"%1\" will move to \"%2\" and \"%1\" will be deleted.\n\nContinue?")
                      .arg(remove->name(), keep->name());
    }

    if (views().isEmpty()) {
        return;
    }
    if (QMessageBox::warning(views().first(), tr("Combine nodes?"), warning, QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    m_undoStack.push(new MergeNodesCommand(this, keep, remove));
}

void GraphScene::pushDeleteSelection() {
    if (selectedItems().isEmpty()) {
        return;
    }
    clearBranchPending();
    std::vector<QPointF> nodes;
    std::vector<BranchKey> branches;
    std::vector<TwoPortKey> twoPorts;
    captureDeleteState(nodes, branches, twoPorts);
    if (nodes.empty() && branches.empty() && twoPorts.empty()) {
        return;
    }
    m_undoStack.push(new DeleteItemsCommand(this));
}

void GraphScene::undo() {
    m_undoStack.undo();
}

void GraphScene::redo() {
    m_undoStack.redo();
}
