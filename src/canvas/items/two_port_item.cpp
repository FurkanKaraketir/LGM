#include "canvas.h"
#include "geometry.h"
#include "item_includes.h"

#include "elemental_equation.h"

#include <QGraphicsScene>
#include <QStyleOptionGraphicsItem>

using namespace ci;

TwoPortItem::TwoPortItem(TwoPortKind kind, const QPointF& center, NodeItem* v1, NodeItem* v2, NodeItem* g1,
                         NodeItem* g2, BranchItem* left, BranchItem* right)
    : m_kind(kind),
      m_center(center),
      m_v1(v1),
      m_v2(v2),
      m_g1(g1),
      m_g2(g2),
      m_left(left),
      m_right(right),
      m_modulus(lg::defaultTwoPortModulus(kind)) {
    setFlags(QGraphicsItem::ItemIsSelectable);
    setZValue(kind == TwoPortKind::Gyrator ? 0.5 : 2.0);
    applyInternalBranchBows();
    refresh();
}

void TwoPortItem::applyInternalBranchBows() {
    if (!m_left || !m_right) {
        return;
    }
    qreal leftMag = twoPortSideBowMag(this, true);
    qreal rightMag = twoPortSideBowMag(this, false);
    capTwoPortBows(this, leftMag, rightMag);
    m_left->setBow(leftMag);
    m_right->setBow(-rightMag);
    syncGeometry();
}

void TwoPortItem::syncGeometry() {
    if (!m_left || !m_right || !m_left->from() || !m_left->to() || !m_right->from() || !m_right->to()) {
        return;
    }
    // Midpoint and direction of each (quadratic, bowed) port branch, computed from the same control
    // point the branch path uses; dashed A-type paths make pointAtPercent() unreliable.
    const auto portMid = [](const BranchItem* branch, QPointF& point, QPointF& tangent) {
        const QPointF a = branch->from()->scenePos();
        const QPointF b = branch->to()->scenePos();
        const QPointF ctrl = bowControlPoint(a, b, branch->bow());
        point = a * 0.25 + ctrl * 0.5 + b * 0.25;
        tangent = b - a;
    };
    QPointF left;
    QPointF right;
    portMid(m_left, left, m_tangentLeft);
    portMid(m_right, right, m_tangentRight);
    const QPainterPath shape = m_kind == TwoPortKind::Gyrator ? gyratorInfinityPath(left, right)
                                                              : transformerCouplerPath(left, right);
    // ponytail: geometry must be announced before boundingRect() starts returning the new shape
    if (shape != m_shape || left != m_couplerLeft || right != m_couplerRight) {
        prepareGeometryChange();
        m_couplerLeft = left;
        m_couplerRight = right;
        m_shape = shape;
    }
    update();
}

void TwoPortItem::setKind(TwoPortKind kind) {
    if (m_kind == kind) {
        return;
    }
    m_kind = kind;
    setZValue(kind == TwoPortKind::Gyrator ? 0.5 : 2.0);
    applyInternalBranchBows();
}

void TwoPortItem::setModulus(const QString& modulus) {
    const QString trimmed = modulus.trimmed();
    const QString next = trimmed.isEmpty() ? lg::defaultTwoPortModulus(m_kind) : trimmed;
    if (m_modulus == next) {
        return;
    }
    m_modulus = next;
    update();
}

QString TwoPortItem::elementalEquationText() const {
    return lg::twoPortElementalEquationText(m_kind, m_modulus, m_left, m_right);
}

void TwoPortItem::applyPortDefaults() {
    m_v1->setName(QStringLiteral("v\u2081"));
    m_v2->setName(QStringLiteral("v\u2082"));
    m_g1->setName(QStringLiteral("ref\u2081"));
    m_g2->setName(QStringLiteral("ref\u2082"));
    // ponytail: keep createBranch/createNode symbolic names (f3, v4, …) — shared f1/f2 breaks multi two-port models

    for (BranchItem* branch : {m_left, m_right}) {
        branch->setBranchType(BranchType::T);
        branch->setActive(false);
        branch->setElementConstant(QStringLiteral("1"));
    }
}

void TwoPortItem::refresh() {
    if (scene()) {
        m_center = static_cast<GraphScene*>(scene())->snap(
            (m_v1->scenePos() + m_v2->scenePos() + m_g1->scenePos() + m_g2->scenePos()) / 4.0);
    }
    applyInternalBranchBows();
    refreshNeighborTwoPortBows(this);
}

void TwoPortItem::moveBy(const QPointF& delta) {
    if (delta.manhattanLength() < 1e-6 || m_syncing) {
        return;
    }
    m_syncing = true;
    QSet<NodeItem*> nodes;
    for (NodeItem* node : {m_v1, m_v2, m_g1, m_g2}) {
        if (node) {
            nodes.insert(node);
        }
    }
    for (NodeItem* node : nodes) {
        node->setPos(node->pos() + delta);
    }
    m_syncing = false;
    m_left->updatePath();
    m_right->updatePath();
    refresh();
}

int TwoPortItem::portRoles(const NodeItem* node) const {
    if (!node) {
        return 0;
    }
    return (node == m_v1 ? 1 : 0) | (node == m_v2 ? 2 : 0) | (node == m_g1 ? 4 : 0) |
           (node == m_g2 ? 8 : 0);
}

void TwoPortItem::replacePortNode(NodeItem* from, NodeItem* to, int roles) {
    if (!to) {
        return;
    }
    if ((roles & 1) && m_v1 == from) {
        m_v1 = to;
    }
    if ((roles & 2) && m_v2 == from) {
        m_v2 = to;
    }
    if ((roles & 4) && m_g1 == from) {
        m_g1 = to;
    }
    if ((roles & 8) && m_g2 == from) {
        m_g2 = to;
    }
}

void TwoPortItem::selectMembers(bool selected) {
    setSelected(selected);
    QGraphicsItem* items[] = {m_v1, m_v2, m_g1, m_g2, m_left, m_right};
    for (QGraphicsItem* item : items) {
        item->setSelected(selected);
    }
}

QRectF TwoPortItem::boundingRect() const {
    // Margin covers the port arrows (they reach ~7 px past the coupler axis) and the pen.
    return m_shape.boundingRect().adjusted(-8.0, -8.0, 8.0, 8.0);
}

QPainterPath TwoPortItem::shape() const {
    return m_shape;
}

void TwoPortItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);

    painter->setPen(QPen(isSelected() ? kSelectionColor : itemInk(widget), 2.0));
    if (m_kind == TwoPortKind::Transformer) {
        drawTransformerCoupler(painter, m_couplerLeft, m_couplerRight, m_tangentLeft, m_tangentRight);
    } else {
        drawGyratorCoupler(painter, m_couplerLeft, m_couplerRight, m_tangentLeft, m_tangentRight);
    }
}

void TwoPortItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (!shape().contains(event->pos())) {
        event->ignore();
        return;
    }
    if (scene()) {
        auto* graph = static_cast<GraphScene*>(scene());
        if (graph->mode() == GraphScene::Mode::SelectNormalTree) {
            event->accept();
            return;
        }
        graph->selectTwoPort(this);
        m_dragScenePos = event->scenePos();
        m_dragCenterStart = center();
        m_dragMoved = false;
    }
    event->accept();
}

void TwoPortItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if ((event->buttons() & Qt::LeftButton) && scene()) {
        const QPointF delta = event->scenePos() - m_dragScenePos;
        if (delta.manhattanLength() > 1e-6) {
            moveBy(delta);
            m_dragScenePos = event->scenePos();
            m_dragMoved = true;
        }
    }
    event->accept();
}

void TwoPortItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    Q_UNUSED(event);
    if (m_dragMoved && scene()) {
        static_cast<GraphScene*>(scene())->pushMoveTwoPort(this, m_dragCenterStart, center());
    }
    m_dragMoved = false;
    event->accept();
}

void TwoPortItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (scene()) {
        auto* graph = static_cast<GraphScene*>(scene());
        if (graph->mode() == GraphScene::Mode::SelectNormalTree) {
            event->accept();
            return;
        }
        graph->pushToggleTwoPortKind(this);
    }
    event->accept();
}
