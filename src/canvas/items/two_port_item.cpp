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
    m_left->setBow(twoPortSideBowMag(this, true));
    m_right->setBow(-twoPortSideBowMag(this, false));
}

void TwoPortItem::setKind(TwoPortKind kind) {
    if (m_kind == kind) {
        return;
    }
    m_kind = kind;
    setZValue(kind == TwoPortKind::Gyrator ? 0.5 : 2.0);
    applyInternalBranchBows();
    update();
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
    prepareGeometryChange();
    update();
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

bool TwoPortItem::collapseSharedRef(NodeItem* gKeep, NodeItem* gRemove) {
    if (!gKeep || !gRemove || gKeep == gRemove) {
        return false;
    }
    if ((gKeep != m_g1 && gKeep != m_g2) || (gRemove != m_g1 && gRemove != m_g2)) {
        return false;
    }
    BranchItem* branch = gRemove == m_g1 ? m_left : m_right;
    branch->replaceEndpoint(gRemove, gKeep);
    if (m_g1 == gRemove) {
        m_g1 = gKeep;
    }
    if (m_g2 == gRemove) {
        m_g2 = gKeep;
    }
    gRemove->setTwoPort(nullptr);
    refresh();
    return true;
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
    return shape().boundingRect().adjusted(-2.0, -2.0, 2.0, 2.0);
}

QPainterPath TwoPortItem::shape() const {
    const QPointF left = couplerLeft();
    const QPointF right = couplerRight();
    if (m_kind == TwoPortKind::Gyrator) {
        return gyratorInfinityPath(left, right);
    }
    const qreal midY = (left.y() + right.y()) / 2.0;
    const qreal span = right.x() - left.x();
    return stadiumPath(
        QRectF(left.x() - kTfPadX, midY - kTfHalfHeight, span + kTfPadX * 2.0, kTfHalfHeight * 2.0));
}

void TwoPortItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);

    const QPointF left = couplerLeft();
    const QPointF right = couplerRight();

    painter->setPen(QPen(isSelected() ? kSelectionColor : itemInk(widget), 2.0));
    if (m_kind == TwoPortKind::Transformer) {
        drawTransformerCoupler(painter, left, right, m_modulus, itemFill(widget));
    } else {
        drawGyratorCoupler(painter, left, right, m_modulus);
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
