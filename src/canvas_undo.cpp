#include "canvas.h"

#include <QUndoCommand>
#include <vector>

namespace {

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
        m_scene->createNode(m_pos);
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
        m_scene->createBranch(a, b);
        const auto between = m_scene->branchesBetween(a, b);
        if (!between.empty()) {
            m_key.index = between.back()->index();
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
        m_scene->createTwoPort(m_key.center, m_key.kind);
        m_exists = true;
    }

private:
    GraphScene* m_scene;
    GraphScene::TwoPortKey m_key;
    bool m_exists = false;
};

class ToggleTwoPortKindCommand : public QUndoCommand {
public:
    ToggleTwoPortKindCommand(GraphScene* scene, const QPointF& center, TwoPortKind from, TwoPortKind to)
        : m_scene(scene), m_center(center), m_from(from), m_to(to) {
        setText("Change two-port type");
    }

    void undo() override { apply(m_from); }

    void redo() override { apply(m_to); }

private:
    void apply(TwoPortKind kind) {
        if (TwoPortItem* item = m_scene->twoPortAtCenter(m_center)) {
            item->setKind(kind);
        }
    }

    GraphScene* m_scene;
    QPointF m_center;
    TwoPortKind m_from;
    TwoPortKind m_to;
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
        if (TwoPortItem* twoPort = m_node->twoPort()) {
            twoPort->moveBy(pos - m_node->pos());
            return;
        }
        m_node->setPos(pos);
        for (BranchItem* branch : m_node->branches()) {
            branch->updatePath();
        }
    }

    GraphScene* m_scene;
    NodeItem* m_node;
    QPointF m_oldPos;
    QPointF m_newPos;
};

}  // namespace

void GraphScene::pushAddNode(const QPointF& center) {
    m_undoStack.push(new AddNodeCommand(this, center));
}

void GraphScene::pushConnectNodes(NodeItem* a, NodeItem* b) {
    m_undoStack.push(new AddBranchCommand(this, a, b));
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
    m_undoStack.push(new ToggleTwoPortKindCommand(this, item->center(), from, to));
}

void GraphScene::pushMove(NodeItem* node, const QPointF& oldPos, const QPointF& newPos) {
    m_undoStack.push(new MoveNodeCommand(this, node, oldPos, newPos));
}

void GraphScene::pushDeleteAt(const QPointF& scenePos) {
    clearBranchPending();
    clearSelection();

    if (TwoPortItem* twoPort = twoPortAt(scenePos)) {
        twoPort->selectMembers(true);
    } else if (NodeItem* node = nodeAt(scenePos)) {
        if (TwoPortItem* twoPort = node->twoPort()) {
            twoPort->selectMembers(true);
        } else {
            node->setSelected(true);
        }
    } else if (BranchItem* branch = branchAt(scenePos)) {
        if (TwoPortItem* twoPort = twoPortFor(branch)) {
            twoPort->selectMembers(true);
        } else {
            branch->setSelected(true);
        }
    } else {
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
