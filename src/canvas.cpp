#include "canvas.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <cmath>

namespace {

constexpr qreal kBowFactor = 0.45;   // arch height as fraction of chord length
constexpr qreal kLaneSpread = 36.0;  // perpendicular gap between parallel branches

QPointF cubicPoint(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, qreal t) {
    const qreal u = 1.0 - t;
    const qreal u2 = u * u;
    const qreal t2 = t * t;
    return u2 * u * p0 + 3.0 * u2 * t * p1 + 3.0 * u * t2 * p2 + t2 * t * p3;
}

QPointF cubicTangent(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, qreal t) {
    const qreal u = 1.0 - t;
    return 3.0 * u * u * (p1 - p0) + 6.0 * u * t * (p2 - p1) + 3.0 * t * t * (p3 - p2);
}

void appendArrowHead(QPainterPath& path, const QPointF& tip, const QPointF& tangent, qreal size = 9.0) {
    const qreal len = std::hypot(tangent.x(), tangent.y());
    if (len < 1e-6) {
        return;
    }
    const QPointF u(tangent.x() / len, tangent.y() / len);
    const QPointF n(-u.y(), u.x());
    const QPointF wing = tip - u * size;
    path.moveTo(tip);
    path.lineTo(wing + n * (size * 0.45));
    path.moveTo(tip);
    path.lineTo(wing - n * (size * 0.45));
}

QPainterPath parallelBranch(const QPointF& a, const QPointF& b, int index, int count) {
    QPainterPath path;
    QPointF dir = b - a;
    const qreal len = std::hypot(dir.x(), dir.y());
    if (len < 1e-6) {
        path.moveTo(a);
        path.lineTo(b);
        return path;
    }

    const QPointF u(dir.x() / len, dir.y() / len);
    const QPointF left(u.y(), -u.x());  // bow to the left of a -> b (up for left-to-right)
    const qreal lane = (count <= 1) ? 0.0 : kLaneSpread * (index - (count - 1) / 2.0);
    const QPointF bow = left * (len * kBowFactor + lane);

    const QPointF c1 = a + u * (len / 3.0) + bow;
    const QPointF c2 = b - u * (len / 3.0) + bow;

    path.moveTo(a);
    path.cubicTo(c1, c2, b);

    const QPointF mid = cubicPoint(a, c1, c2, b, 0.5);
    appendArrowHead(path, mid, cubicTangent(a, c1, c2, b, 0.5));
    return path;
}

}  // namespace

BranchItem::BranchItem(NodeItem* from, NodeItem* to, int index, int count)
    : m_from(from), m_to(to), m_index(index), m_count(count) {
    setPen(QPen(Qt::black, 2));
    setZValue(0);
    updatePath();
}

void BranchItem::setSlot(int index, int count) {
    m_index = index;
    m_count = count;
    updatePath();
}

void BranchItem::updatePath() {
    setPath(parallelBranch(m_from->scenePos(), m_to->scenePos(), m_index, m_count));
}

NodeItem::NodeItem(qreal radius) : QGraphicsEllipseItem(-radius, -radius, radius * 2, radius * 2) {
    setBrush(Qt::white);
    setPen(QPen(Qt::black, 2));
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemSendsGeometryChanges);
    setZValue(1);
}

void NodeItem::addBranch(BranchItem* branch) {
    m_branches.push_back(branch);
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionChange && scene()) {
        const auto* graph = static_cast<GraphScene*>(scene());
        return graph->snap(value.toPointF());
    }
    if (change == ItemPositionHasChanged) {
        for (BranchItem* branch : m_branches) {
            branch->updatePath();
        }
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}

GraphScene::GraphScene(QObject* parent) : QGraphicsScene(-2000, -2000, 4000, 4000, parent) {}

NodeItem* GraphScene::addNode(const QPointF& center) {
    auto* node = new NodeItem;
    node->setPos(snap(center));
    addItem(node);
    return node;
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
    std::vector<BranchItem*> branches = branchesBetween(a, b);
    const int count = static_cast<int>(branches.size());
    for (int i = 0; i < count; ++i) {
        branches[i]->setSlot(i, count);
    }
}

void GraphScene::connectNodes(NodeItem* a, NodeItem* b) {
    if (a == b) {
        return;
    }
    const int count = static_cast<int>(branchesBetween(a, b).size()) + 1;
    auto* branch = new BranchItem(a, b, count - 1, count);
    a->addBranch(branch);
    b->addBranch(branch);
    addItem(branch);
    reindexBranches(a, b);
}

QPointF GraphScene::snap(QPointF point) const {
    return {std::round(point.x() / kGrid) * kGrid, std::round(point.y() / kGrid) * kGrid};
}

void GraphScene::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, QColor(250, 250, 250));

    const qreal left = std::floor(rect.left() / kGrid) * kGrid;
    const qreal top = std::floor(rect.top() / kGrid) * kGrid;

    painter->setPen(QColor(200, 200, 200));
    for (qreal x = left; x < rect.right(); x += kGrid) {
        for (qreal y = top; y < rect.bottom(); y += kGrid) {
            painter->drawPoint(QPointF(x, y));
        }
    }
}

void GraphScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !(event->modifiers() & Qt::ShiftModifier)) {
        if (m_pending && event->button() == Qt::LeftButton) {
            auto* node = dynamic_cast<NodeItem*>(itemAt(event->scenePos(), QTransform()));
            if (!node || node != m_pending) {
                m_pending->setSelected(false);
                m_pending = nullptr;
            }
        }
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    auto* node = dynamic_cast<NodeItem*>(itemAt(event->scenePos(), QTransform()));
    if (!node) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    if (!m_pending) {
        m_pending = node;
        node->setSelected(true);
        event->accept();
        return;
    }

    if (m_pending == node) {
        m_pending->setSelected(false);
        m_pending = nullptr;
        event->accept();
        return;
    }

    connectNodes(m_pending, node);
    m_pending->setSelected(false);
    m_pending = nullptr;
    event->accept();
}

void GraphScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (!itemAt(event->scenePos(), QTransform())) {
        addNode(event->scenePos());
        event->accept();
        return;
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}
