#include "canvas.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <algorithm>
#include <cmath>

namespace {

constexpr qreal kBowFactor = 0.20;
constexpr qreal kLaneSpread = 36.0;

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

QPointF quadTangent(const QPointF& p0, const QPointF& c, const QPointF& p1, qreal t) {
    const qreal u = 1.0 - t;
    return 2.0 * u * (c - p0) + 2.0 * t * (p1 - c);
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

void drawDownArrow(QPainter* painter, const QPointF& tip, qreal size = 7.0) {
    painter->drawLine(tip, tip + QPointF(0.0, size));
    painter->drawLine(tip + QPointF(0.0, size), tip + QPointF(-size * 0.4, size * 0.55));
    painter->drawLine(tip + QPointF(0.0, size), tip + QPointF(size * 0.4, size * 0.55));
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
    const QPointF left(u.y(), -u.x());
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

QPainterPath bowedBranch(const QPointF& top, const QPointF& bottom, qreal bow) {
    QPainterPath path;
    const QPointF ctrl = (top + bottom) / 2.0 + QPointF(bow, 0.0);
    path.moveTo(top);
    path.quadTo(ctrl, bottom);

    const QPointF midActual((1 - 0.5) * (1 - 0.5) * top.x() + 2 * 0.5 * (1 - 0.5) * ctrl.x() + 0.5 * 0.5 * bottom.x(),
                            (1 - 0.5) * (1 - 0.5) * top.y() + 2 * 0.5 * (1 - 0.5) * ctrl.y() + 0.5 * 0.5 * bottom.y());
    appendArrowHead(path, midActual, quadTangent(top, ctrl, bottom, 0.5));
    return path;
}

void drawGroundSymbol(QPainter* painter, const QPointF& center, qreal radius) {
    const QPointF base(center.x(), center.y() + radius + 2.0);
    painter->drawLine(base, base + QPointF(0.0, 8.0));
    for (int i = 0; i < 3; ++i) {
        const qreal inset = i * 3.0;
        painter->drawLine(base + QPointF(-8.0 + inset, 8.0 + i * 3.0),
                          base + QPointF(8.0 - inset, 8.0 + i * 3.0));
    }
}

void drawTransformerCoupler(QPainter* painter, const QPointF& left, const QPointF& right) {
    const qreal midY = (left.y() + right.y()) / 2.0;
    const qreal padX = 6.0;
    const QRectF box(left.x() - padX, midY - 14.0, right.x() - left.x() + padX * 2.0, 28.0);
    painter->setBrush(Qt::white);
    painter->drawRect(box);

    const qreal span = right.x() - left.x();
    drawDownArrow(painter, QPointF(left.x() + span * 0.25, midY - 2.0));
    drawDownArrow(painter, QPointF(right.x() - span * 0.25, midY - 2.0));
}

void drawGyratorCoupler(QPainter* painter, const QPointF& left, const QPointF& right) {
    const qreal midY = (left.y() + right.y()) / 2.0;
    const qreal span = right.x() - left.x();
    const qreal h = 16.0;

    QPainterPath loop;
    loop.moveTo(left);
    loop.cubicTo(left + QPointF(span * 0.18, -h), left + QPointF(span * 0.42, -h),
                 QPointF(left.x() + span * 0.5, midY));
    loop.cubicTo(left + QPointF(span * 0.58, h), left + QPointF(span * 0.82, h), right);
    loop.cubicTo(right + QPointF(-span * 0.18, h), right + QPointF(-span * 0.42, h),
                 QPointF(right.x() - span * 0.5, midY));
    loop.cubicTo(right + QPointF(-span * 0.58, -h), right + QPointF(-span * 0.82, -h), left);
    painter->drawPath(loop);

    drawDownArrow(painter, QPointF(left.x() + span * 0.25, midY - 2.0));
    drawDownArrow(painter, QPointF(right.x() - span * 0.25, midY - 2.0));
}

}  // namespace

BranchItem::BranchItem(NodeItem* from, NodeItem* to, int index, int count, qreal bow)
    : m_from(from), m_to(to), m_index(index), m_count(count), m_bow(bow) {
    setPen(QPen(Qt::black, 2));
    setFlags(QGraphicsItem::ItemIsSelectable);
    setZValue(0);
    updatePath();
}

void BranchItem::setSlot(int index, int count) {
    m_index = index;
    m_count = count;
    updatePath();
}

void BranchItem::setBow(qreal bow) {
    m_bow = bow;
    updatePath();
}

void BranchItem::updatePath() {
    if (!m_from || !m_to || !m_from->scene() || !m_to->scene()) {
        return;
    }
    const QPointF a = m_from->scenePos();
    const QPointF b = m_to->scenePos();
    if (std::abs(m_bow) > 1e-6) {
        setPath(bowedBranch(a, b, m_bow));
    } else {
        setPath(parallelBranch(a, b, m_index, m_count));
    }
}

QVariant BranchItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemSelectedHasChanged) {
        setPen(isSelected() ? QPen(QColor(0, 100, 200), 2) : QPen(Qt::black, 2));
    }
    return QGraphicsPathItem::itemChange(change, value);
}

NodeItem::NodeItem(qreal radius) : QGraphicsEllipseItem(-radius, -radius, radius * 2, radius * 2) {
    setBrush(Qt::white);
    setPen(QPen(Qt::black, 2));
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemSendsGeometryChanges);
    setZValue(1);
}

void NodeItem::setGround(bool ground) {
    m_ground = ground;
    update();
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    QGraphicsEllipseItem::paint(painter, option, widget);
    if (m_ground) {
        painter->setPen(QPen(Qt::black, 1.5));
        drawGroundSymbol(painter, QPointF(0.0, 0.0), rect().width() / 2.0);
    }
}

void NodeItem::addBranch(BranchItem* branch) {
    m_branches.push_back(branch);
}

void NodeItem::removeBranch(BranchItem* branch) {
    auto it = std::find(m_branches.begin(), m_branches.end(), branch);
    if (it != m_branches.end()) {
        m_branches.erase(it);
    }
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionChange && scene()) {
        const auto* graph = static_cast<GraphScene*>(scene());
        const QPointF snapped = graph->snap(value.toPointF());
        if (m_twoPort) {
            if (m_twoPort->isSyncing()) {
                return snapped;
            }
            m_twoPort->moveBy(snapped - pos());
            return snapped;
        }
        return snapped;
    }
    if (change == ItemPositionHasChanged) {
        for (BranchItem* branch : m_branches) {
            branch->updatePath();
        }
        if (m_twoPort) {
            m_twoPort->refresh();
        }
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    m_dragStart = pos();
    if (m_twoPort && scene()) {
        auto* graph = static_cast<GraphScene*>(scene());
        if (graph->mode() == GraphScene::Mode::Select) {
            graph->selectTwoPort(m_twoPort);
        }
    }
    QGraphicsEllipseItem::mousePressEvent(event);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsEllipseItem::mouseReleaseEvent(event);
    if (!scene()) {
        return;
    }
    auto* graph = static_cast<GraphScene*>(scene());
    if (graph->mode() == GraphScene::Mode::Select && m_dragStart != pos()) {
        graph->pushMove(this, m_dragStart, pos());
    }
}

TwoPortItem::TwoPortItem(TwoPortKind kind, const QPointF& center, NodeItem* v1, NodeItem* v2, NodeItem* g1,
                         NodeItem* g2, BranchItem* left, BranchItem* right)
    : m_kind(kind), m_center(center), m_v1(v1), m_v2(v2), m_g1(g1), m_g2(g2), m_left(left), m_right(right) {
    setFlags(QGraphicsItem::ItemIsSelectable);
    setZValue(2);
    refresh();
}

void TwoPortItem::setKind(TwoPortKind kind) {
    m_kind = kind;
    update();
}

void TwoPortItem::refresh() {
    if (scene()) {
        m_center = static_cast<GraphScene*>(scene())->snap(
            (m_v1->scenePos() + m_v2->scenePos() + m_g1->scenePos() + m_g2->scenePos()) / 4.0);
    }
    prepareGeometryChange();
    update();
}

void TwoPortItem::moveBy(const QPointF& delta) {
    if (delta.manhattanLength() < 1e-6 || m_syncing) {
        return;
    }
    m_syncing = true;
    for (NodeItem* node : {m_v1, m_v2, m_g1, m_g2}) {
        node->setPos(node->pos() + delta);
    }
    m_syncing = false;
    m_left->updatePath();
    m_right->updatePath();
    refresh();
}

void TwoPortItem::selectMembers(bool selected) {
    setSelected(selected);
    QGraphicsItem* items[] = {m_v1, m_v2, m_g1, m_g2, m_left, m_right};
    for (QGraphicsItem* item : items) {
        item->setSelected(selected);
    }
}

QPointF TwoPortItem::couplerLeft() const {
    return m_left->path().pointAtPercent(0.5);
}

QPointF TwoPortItem::couplerRight() const {
    return m_right->path().pointAtPercent(0.5);
}

QRectF TwoPortItem::boundingRect() const {
    const QPointF left = couplerLeft();
    const QPointF right = couplerRight();
    const qreal midY = (left.y() + right.y()) / 2.0;
    return QRectF(left.x() - 24.0, midY - 48.0, right.x() - left.x() + 48.0, 96.0);
}

QPainterPath TwoPortItem::shape() const {
    QPainterPath path;
    path.addRect(boundingRect());
    return path;
}

void TwoPortItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    const QPointF left = couplerLeft();
    const QPointF right = couplerRight();

    painter->setPen(QPen(isSelected() ? QColor(0, 100, 200) : Qt::black, 2.0));
    if (m_kind == TwoPortKind::Transformer) {
        drawTransformerCoupler(painter, left, right);
    } else {
        drawGyratorCoupler(painter, left, right);
    }
}

void TwoPortItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (scene()) {
        static_cast<GraphScene*>(scene())->selectTwoPort(this);
    }
    QGraphicsItem::mousePressEvent(event);
}

void TwoPortItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (scene()) {
        auto* graph = static_cast<GraphScene*>(scene());
        graph->pushToggleTwoPortKind(this);
    }
    event->accept();
}
