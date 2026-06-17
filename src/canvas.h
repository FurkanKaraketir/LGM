#pragma once

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <vector>

class NodeItem;

class BranchItem : public QGraphicsPathItem {
public:
    BranchItem(NodeItem* from, NodeItem* to, int index, int count);

    void updatePath();
    NodeItem* from() const { return m_from; }
    NodeItem* to() const { return m_to; }
    void setSlot(int index, int count);

private:
    NodeItem* m_from;
    NodeItem* m_to;
    int m_index;
    int m_count;
};

class NodeItem : public QGraphicsEllipseItem {
public:
    explicit NodeItem(qreal radius = 8.0);

    void addBranch(BranchItem* branch);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    std::vector<BranchItem*> m_branches;
};

class GraphScene : public QGraphicsScene {
public:
    explicit GraphScene(QObject* parent = nullptr);

    NodeItem* addNode(const QPointF& center);
    void connectNodes(NodeItem* a, NodeItem* b);
    QPointF snap(QPointF point) const;

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void reindexBranches(NodeItem* a, NodeItem* b);
    std::vector<BranchItem*> branchesBetween(NodeItem* a, NodeItem* b) const;

    static constexpr qreal kGrid = 20.0;

    NodeItem* m_pending = nullptr;
};
