#include "canvas.h"

#include "elemental_equation.h"

#include "elemental_equation.h"

#include <QApplication>
#include <QComboBox>
#include <QPalette>
#include <QFontMetricsF>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QLineEdit>
#include <QLabel>
#include <QPainter>
#include <QPainterPathStroker>
#include <QRadioButton>
#include <QStyleOptionGraphicsItem>
#include <QVBoxLayout>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace ci {

constexpr qreal kBowFactor = 0.24;
constexpr qreal kLaneSpread = 36.0;
constexpr qreal kBranchPickWidth = 6.0;

QPainterPath strokedPickShape(const QPainterPath& source, qreal width = kBranchPickWidth) {
    if (source.isEmpty()) {
        return source;
    }
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(source);
}
constexpr qreal kTwoPortEgressKick = 48.0;
constexpr qreal kInterTwoPortClearance = 20.0;

QPalette itemPalette(const QWidget* widget) {
    return widget ? widget->palette() : QApplication::palette();
}

QColor itemInk(const QWidget* widget) {
    return itemPalette(widget).color(QPalette::WindowText);
}

QColor itemFill(const QWidget* widget) {
    return itemPalette(widget).color(QPalette::Base);
}

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

void appendArrowHead(QPainterPath& path, const QPointF& tip, const QPointF& tangent, qreal size = 9.0, bool withCircle = false) {
    const qreal len = std::hypot(tangent.x(), tangent.y());
    if (len < 1e-6) {
        return;
    }
    const QPointF u(tangent.x() / len, tangent.y() / len);
    const QPointF n(-u.y(), u.x());

    if (withCircle) {
        // ponytail: current-source symbol: circle + centered arrow
        const qreal circleRadius = 14.0;
        path.addEllipse(tip, circleRadius, circleRadius);

        const qreal arrowLen = circleRadius * 0.85;
        const QPointF arrowBase = tip - u * arrowLen * 0.5;
        const QPointF arrowTip = tip + u * arrowLen * 0.5;
        const qreal wingSize = arrowLen * 0.35;

        path.moveTo(arrowBase);
        path.lineTo(arrowTip);
        path.lineTo(arrowTip - u * wingSize + n * wingSize * 0.45);
        path.moveTo(arrowTip);
        path.lineTo(arrowTip - u * wingSize - n * wingSize * 0.45);
    } else {
        const QPointF wing = tip - u * size;
        path.moveTo(tip);
        path.lineTo(wing + n * (size * 0.45));
        path.moveTo(tip);
        path.lineTo(wing - n * (size * 0.45));
    }
}

QPainterPath filledArrowHead(const QPointF& tip, const QPointF& tangent, qreal size = 8.0) {
    QPainterPath head;
    const qreal len = std::hypot(tangent.x(), tangent.y());
    if (len < 1e-6) {
        return head;
    }
    const QPointF u(tangent.x() / len, tangent.y() / len);
    const QPointF n(-u.y(), u.x());
    const QPointF base = tip - u * size;
    head.moveTo(tip);
    head.lineTo(base + n * (size * 0.42));
    head.lineTo(base - n * (size * 0.42));
    head.closeSubpath();
    return head;
}

void drawDownArrow(QPainter* painter, const QPointF& top, qreal stemLen = 11.0) {
    const QPointF bottom = top + QPointF(0.0, stemLen);
    painter->drawLine(top, bottom);
    QPainterPath head;
    head.moveTo(bottom);
    head.lineTo(bottom + QPointF(-4.5, -5.5));
    head.lineTo(bottom + QPointF(4.5, -5.5));
    head.closeSubpath();
    painter->fillPath(head, painter->pen().color());
}

QPainterPath stadiumPath(const QRectF& rect) {
    const qreal radius = rect.height() * 0.5;
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    return path;
}

constexpr qreal kTwoPortPortBow = 28.0;
constexpr qreal kTwoPortPortBowPerBranch = 16.0;
constexpr qreal kTwoPortPortBowNeighbor = 22.0;
constexpr qreal kTfHalfHeight = 6.0;
constexpr qreal kTfPadX = 2.0;

QPainterPath parallelBranch(const QPointF& a, const QPointF& b, int index, int count,
                            const QPointF& kickA = {}, const QPointF& kickB = {}, bool active = false,
                            bool dashedTail = false, bool drawArrow = true) {
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

    const QPointF c1 = a + u * (len / 3.0) + bow + kickA;
    const QPointF c2 = b - u * (len / 3.0) + bow + kickB;

    if (dashedTail) {
        QPainterPath solidPart;
        solidPart.moveTo(a);
        for (qreal t = 0.0; t <= 0.5; t += 0.01) {
            solidPart.lineTo(cubicPoint(a, c1, c2, b, t));
        }
        path.addPath(solidPart);
        
        QPainterPath dashedPart;
        const qreal dashLen = 8.0;
        const qreal gapLen = 6.0;
        qreal distance = 0.0;
        bool drawing = true;
        QPointF lastPt = cubicPoint(a, c1, c2, b, 0.5);
        
        for (qreal t = 0.5; t <= 1.0; t += 0.005) {
            QPointF pt = cubicPoint(a, c1, c2, b, t);
            qreal segLen = std::hypot(pt.x() - lastPt.x(), pt.y() - lastPt.y());
            distance += segLen;
            
            if (drawing && distance >= dashLen) {
                distance = 0.0;
                drawing = false;
            } else if (!drawing && distance >= gapLen) {
                distance = 0.0;
                drawing = true;
                dashedPart.moveTo(pt);
            }
            
            if (drawing) {
                if (dashedPart.isEmpty()) {
                    dashedPart.moveTo(lastPt);
                }
                dashedPart.lineTo(pt);
            }
            lastPt = pt;
        }
        path.addPath(dashedPart);
    } else {
        path.moveTo(a);
        path.cubicTo(c1, c2, b);
    }

    const QPointF mid = cubicPoint(a, c1, c2, b, 0.5);
    if (drawArrow && active) {
        appendArrowHead(path, mid, cubicTangent(a, c1, c2, b, 0.5), 9.0, true);
    }
    return path;
}

QPainterPath bowedBranch(const QPointF& top, const QPointF& bottom, qreal bow, bool active = false,
                         bool dashedTail = false, bool drawArrow = true) {
    QPainterPath path;
    const QPointF ctrl = (top + bottom) / 2.0 + QPointF(bow, 0.0);
    
    if (dashedTail) {
        QPainterPath solidPart;
        solidPart.moveTo(top);
        for (qreal t = 0.0; t <= 0.5; t += 0.02) {
            qreal x = (1 - t) * (1 - t) * top.x() + 2 * t * (1 - t) * ctrl.x() + t * t * bottom.x();
            qreal y = (1 - t) * (1 - t) * top.y() + 2 * t * (1 - t) * ctrl.y() + t * t * bottom.y();
            solidPart.lineTo(QPointF(x, y));
        }
        path.addPath(solidPart);
        
        QPainterPath dashedPart;
        const qreal dashLen = 8.0;
        const qreal gapLen = 6.0;
        qreal distance = 0.0;
        bool drawing = true;
        qreal prevX = (1 - 0.5) * (1 - 0.5) * top.x() + 2 * 0.5 * (1 - 0.5) * ctrl.x() + 0.5 * 0.5 * bottom.x();
        qreal prevY = (1 - 0.5) * (1 - 0.5) * top.y() + 2 * 0.5 * (1 - 0.5) * ctrl.y() + 0.5 * 0.5 * bottom.y();
        
        for (qreal t = 0.5; t <= 1.0; t += 0.01) {
            qreal x = (1 - t) * (1 - t) * top.x() + 2 * t * (1 - t) * ctrl.x() + t * t * bottom.x();
            qreal y = (1 - t) * (1 - t) * top.y() + 2 * t * (1 - t) * ctrl.y() + t * t * bottom.y();
            qreal segLen = std::hypot(x - prevX, y - prevY);
            distance += segLen;
            
            if (drawing && distance >= dashLen) {
                distance = 0.0;
                drawing = false;
            } else if (!drawing && distance >= gapLen) {
                distance = 0.0;
                drawing = true;
                dashedPart.moveTo(QPointF(x, y));
            }
            
            if (drawing) {
                if (dashedPart.isEmpty()) {
                    dashedPart.moveTo(QPointF(prevX, prevY));
                }
                dashedPart.lineTo(QPointF(x, y));
            }
            prevX = x;
            prevY = y;
        }
        path.addPath(dashedPart);
    } else {
        path.moveTo(top);
        path.quadTo(ctrl, bottom);
    }

    const QPointF midActual((1 - 0.5) * (1 - 0.5) * top.x() + 2 * 0.5 * (1 - 0.5) * ctrl.x() + 0.5 * 0.5 * bottom.x(),
                            (1 - 0.5) * (1 - 0.5) * top.y() + 2 * 0.5 * (1 - 0.5) * ctrl.y() + 0.5 * 0.5 * bottom.y());
    if (drawArrow && active) {
        appendArrowHead(path, midActual, quadTangent(top, ctrl, bottom, 0.5), 9.0, true);
    }
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

void drawTransformerCoupler(QPainter* painter, const QPointF& left, const QPointF& right,
                            const QString& modulus, const QColor& fill) {
    Q_UNUSED(modulus);
    Q_UNUSED(fill);
    const qreal midY = (left.y() + right.y()) / 2.0;
    const QRectF box(left.x() - kTfPadX, midY - kTfHalfHeight, right.x() - left.x() + kTfPadX * 2.0,
                     kTfHalfHeight * 2.0);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(stadiumPath(box));

    drawDownArrow(painter, QPointF(left.x(), midY - 4.0));
    drawDownArrow(painter, QPointF(right.x(), midY - 4.0));
}

void drawGyratorCoupler(QPainter* painter, const QPointF& left, const QPointF& right,
                        const QString& modulus) {
    Q_UNUSED(modulus);
    const qreal midY = (left.y() + right.y()) / 2.0;
    const QPointF center((left.x() + right.x()) / 2.0, midY);
    const qreal w = 12.0;
    const qreal h = 17.0;

    QPainterPath loop;
    loop.moveTo(left);
    loop.cubicTo(QPointF(left.x() - w, midY - h * 0.55), QPointF(center.x() - w * 0.45, midY - h),
                 center);
    loop.cubicTo(QPointF(center.x() + w * 0.45, midY + h), QPointF(right.x() + w, midY + h * 0.55),
                 right);
    loop.cubicTo(QPointF(right.x() + w, midY - h * 0.55), QPointF(center.x() + w * 0.45, midY - h),
                 center);
    loop.cubicTo(QPointF(center.x() - w * 0.45, midY + h), QPointF(left.x() - w, midY + h * 0.55),
                 left);

    QPen loopPen = painter->pen();
    loopPen.setWidthF(1.5);
    painter->setPen(loopPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(loop);

    painter->setPen(QPen(loopPen.color(), 2.0));
    drawDownArrow(painter, left + QPointF(0.0, -5.0));
    drawDownArrow(painter, right + QPointF(0.0, -5.0));
}

QPainterPath gyratorInfinityPath(const QPointF& left, const QPointF& right) {
    const qreal midY = (left.y() + right.y()) / 2.0;
    const QPointF center((left.x() + right.x()) / 2.0, midY);
    const qreal w = 12.0;
    const qreal h = 17.0;

    QPainterPath loop;
    loop.moveTo(left);
    loop.cubicTo(QPointF(left.x() - w, midY - h * 0.55), QPointF(center.x() - w * 0.45, midY - h),
                 center);
    loop.cubicTo(QPointF(center.x() + w * 0.45, midY + h), QPointF(right.x() + w, midY + h * 0.55),
                 right);
    loop.cubicTo(QPointF(right.x() + w, midY - h * 0.55), QPointF(center.x() + w * 0.45, midY - h),
                 center);
    loop.cubicTo(QPointF(center.x() - w * 0.45, midY + h), QPointF(left.x() - w, midY + h * 0.55),
                 left);
    return loop;
}

struct BranchArrowGeom {
    QPointF tip;
    QPointF tangent;
};

bool isTwoPortPortBranch(const BranchItem* branch) {
    return branch && branch->isTwoPortPort();
}

bool isInterTwoPortLink(const BranchItem* branch, TwoPortItem** outA = nullptr,
                        TwoPortItem** outB = nullptr) {
    if (!branch || isTwoPortPortBranch(branch) || !branch->from() || !branch->to()) {
        return false;
    }
    TwoPortItem* tpA = branch->from()->twoPort();
    TwoPortItem* tpB = branch->to()->twoPort();
    if (!tpA || !tpB || tpA == tpB) {
        return false;
    }
    if (outA) {
        *outA = tpA;
    }
    if (outB) {
        *outB = tpB;
    }
    return true;
}

bool isFacingNeighborLink(const TwoPortItem* tp, const NodeItem* port, const BranchItem* branch) {
    if (!tp || !port || !branch) {
        return false;
    }
    TwoPortItem* tpA = nullptr;
    TwoPortItem* tpB = nullptr;
    if (!isInterTwoPortLink(branch, &tpA, &tpB)) {
        return false;
    }
    TwoPortItem* otherTp = tpA == tp ? tpB : (tpB == tp ? tpA : nullptr);
    if (!otherTp) {
        return false;
    }
    const qreal dx = otherTp->center().x() - tp->center().x();
    const bool portOnLeft = port == tp->v1() || port == tp->g1();
    const bool portOnRight = port == tp->v2() || port == tp->g2();
    return (portOnRight && dx > 0.0) || (portOnLeft && dx < 0.0);
}

bool hasFacingNeighborLinkOnSide(const TwoPortItem* tp, bool leftSide) {
    for (NodeItem* port : {leftSide ? tp->v1() : tp->v2(), leftSide ? tp->g1() : tp->g2()}) {
        if (!port) {
            continue;
        }
        for (BranchItem* branch : port->branches()) {
            if (isFacingNeighborLink(tp, port, branch)) {
                return true;
            }
        }
    }
    return false;
}

int externalOutwardBranchCountAt(const TwoPortItem* tp, bool leftSide) {
    int count = 0;
    for (NodeItem* port : {leftSide ? tp->v1() : tp->v2(), leftSide ? tp->g1() : tp->g2()}) {
        if (!port) {
            continue;
        }
        for (BranchItem* branch : port->branches()) {
            if (isTwoPortPortBranch(branch) || isFacingNeighborLink(tp, port, branch)) {
                continue;
            }
            ++count;
        }
    }
    return count;
}

bool hasNeighborTwoPortOn(const TwoPortItem* tp, bool right) {
    if (!tp || !tp->scene()) {
        return false;
    }
    const qreal cx = tp->center().x();
    const qreal cy = tp->center().y();
    const qreal minDx = GraphScene::kTwoPortHalfWidth * 1.25;
    const qreal maxDx = GraphScene::kTwoPortHalfWidth * 3.5;
    for (QGraphicsItem* item : tp->scene()->items()) {
        auto* other = dynamic_cast<TwoPortItem*>(item);
        if (!other || other == tp) {
            continue;
        }
        const QPointF oc = other->center();
        if (std::abs(oc.y() - cy) > GraphScene::kTwoPortHalfHeight) {
            continue;
        }
        const qreal dx = oc.x() - cx;
        if (right && dx > minDx && dx < maxDx) {
            return true;
        }
        if (!right && dx < -minDx && dx > -maxDx) {
            return true;
        }
    }
    return false;
}

qreal twoPortSideBowMag(const TwoPortItem* tp, bool leftSide) {
    qreal mag = kTwoPortPortBow;
    mag += kTwoPortPortBowPerBranch * externalOutwardBranchCountAt(tp, leftSide);
    if (leftSide && hasNeighborTwoPortOn(tp, false) && !hasFacingNeighborLinkOnSide(tp, true)) {
        mag += kTwoPortPortBowNeighbor;
    }
    if (!leftSide && hasNeighborTwoPortOn(tp, true) && !hasFacingNeighborLinkOnSide(tp, false)) {
        mag += kTwoPortPortBowNeighbor;
    }
    return mag;
}

void refreshNeighborTwoPortBows(TwoPortItem* tp) {
    if (!tp || !tp->scene()) {
        return;
    }
    const qreal cx = tp->center().x();
    const qreal cy = tp->center().y();
    const qreal maxDx = GraphScene::kTwoPortHalfWidth * 3.5;
    for (QGraphicsItem* item : tp->scene()->items()) {
        auto* other = dynamic_cast<TwoPortItem*>(item);
        if (!other || other == tp) {
            continue;
        }
        const QPointF oc = other->center();
        if (std::abs(oc.y() - cy) > GraphScene::kTwoPortHalfHeight) {
            continue;
        }
        if (std::abs(oc.x() - cx) < maxDx) {
            other->applyInternalBranchBows();
        }
    }
}

qreal externalBranchLane(int index, int count) {
    return (count <= 1) ? 0.0 : kLaneSpread * 0.55 * (index - (count - 1) / 2.0);
}

QPointF interTwoPortKick(const TwoPortItem* tp, const NodeItem* port) {
    const bool topPort = port == tp->v1() || port == tp->v2();
    return QPointF(0.0, topPort ? -kInterTwoPortClearance : kInterTwoPortClearance);
}

QPointF twoPortKick(const TwoPortItem* tp, const NodeItem* port, const BranchItem* branch, qreal lane,
                    qreal chordLen) {
    if (!tp || !port) {
        return {};
    }
    if (branch && isFacingNeighborLink(tp, port, branch)) {
        return interTwoPortKick(tp, port) + QPointF(0.0, lane);
    }

    qreal mag = kTwoPortEgressKick;
    if (chordLen > 1e-6) {
        mag = std::min(kTwoPortEgressKick, chordLen * 0.42);
    }

    QPointF lateral;
    if (port == tp->v1() || port == tp->g1()) {
        lateral = QPointF(-mag, 0.0);
    } else if (port == tp->v2() || port == tp->g2()) {
        lateral = QPointF(mag, 0.0);
    } else {
        QPointF away = port->scenePos() - tp->center();
        const qreal len = std::hypot(away.x(), away.y());
        if (len < 1e-6) {
            return {};
        }
        lateral = QPointF(away.x() / len * mag, 0.0);
    }

    return lateral + QPointF(0.0, lane);
}

void externalLaneAtPort(const NodeItem* port, const BranchItem* branch, int& index, int& count) {
    index = 0;
    count = 1;
    if (!port || !port->twoPort() || isTwoPortPortBranch(branch)) {
        return;
    }

    std::vector<BranchItem*> external;
    external.reserve(port->branches().size());
    for (BranchItem* b : port->branches()) {
        if (!isTwoPortPortBranch(b)) {
            external.push_back(b);
        }
    }
    count = static_cast<int>(external.size());
    if (count <= 1) {
        return;
    }

    const QPointF pos = port->scenePos();
    const auto otherPos = [&](const BranchItem* b) {
        return b->from() == port ? b->to()->scenePos() : b->from()->scenePos();
    };
    std::sort(external.begin(), external.end(), [&](const BranchItem* lhs, const BranchItem* rhs) {
        const QPointF dl = otherPos(lhs) - pos;
        const QPointF dr = otherPos(rhs) - pos;
        return std::atan2(dl.y(), dl.x()) < std::atan2(dr.y(), dr.x());
    });
    for (int i = 0; i < count; ++i) {
        if (external[static_cast<size_t>(i)] == branch) {
            index = i;
            return;
        }
    }
}

struct BranchEgress {
    QPointF kickA;
    QPointF kickB;
};

BranchEgress computeBranchEgress(const BranchItem* branch) {
    BranchEgress egress;
    if (!branch || isTwoPortPortBranch(branch) || !branch->from() || !branch->to()) {
        return egress;
    }
    if (!branch->from()->scene() || !branch->to()->scene()) {
        return egress;
    }

    const QPointF a = branch->from()->scenePos();
    const QPointF b = branch->to()->scenePos();
    const qreal chordLen = std::hypot(b.x() - a.x(), b.y() - a.y());

    if (NodeItem* from = branch->from(); from && from->twoPort()) {
        int index = 0;
        int count = 1;
        externalLaneAtPort(from, branch, index, count);
        egress.kickA =
            twoPortKick(from->twoPort(), from, branch, externalBranchLane(index, count), chordLen);
    }
    if (NodeItem* to = branch->to(); to && to->twoPort()) {
        int index = 0;
        int count = 1;
        externalLaneAtPort(to, branch, index, count);
        egress.kickB =
            twoPortKick(to->twoPort(), to, branch, externalBranchLane(index, count), chordLen);
    }
    return egress;
}

QString branchAnnotationLabel(const BranchItem* branch) {
    if (isTwoPortPortBranch(branch)) {
        return branch->name();
    }
    if (branch->isActive()) {
        return lg::branchSourceInputSymbol(*branch);
    }
    const QString constant = branch->elementConstant().trimmed();
    return constant.isEmpty() ? QStringLiteral("1") : constant;
}

BranchArrowGeom branchArrowGeom(const QPointF& a, const QPointF& b, int index, int count, qreal bow,
                                const QPointF& kickA = {}, const QPointF& kickB = {}) {
    if (std::abs(bow) > 1e-6) {
        const QPointF ctrl = (a + b) / 2.0 + QPointF(bow, 0.0);
        return {
            QPointF((1 - 0.5) * (1 - 0.5) * a.x() + 2 * 0.5 * (1 - 0.5) * ctrl.x() + 0.5 * 0.5 * b.x(),
                    (1 - 0.5) * (1 - 0.5) * a.y() + 2 * 0.5 * (1 - 0.5) * ctrl.y() + 0.5 * 0.5 * b.y()),
            quadTangent(a, ctrl, b, 0.5),
        };
    }

    QPointF dir = b - a;
    const qreal len = std::hypot(dir.x(), dir.y());
    if (len < 1e-6) {
        return {a, QPointF(1.0, 0.0)};
    }
    const QPointF u(dir.x() / len, dir.y() / len);
    const QPointF left(u.y(), -u.x());
    const qreal lane = (count <= 1) ? 0.0 : kLaneSpread * (index - (count - 1) / 2.0);
    const QPointF bowOffset = left * (len * kBowFactor + lane);
    const QPointF c1 = a + u * (len / 3.0) + bowOffset + kickA;
    const QPointF c2 = b - u * (len / 3.0) + bowOffset + kickB;
    return {cubicPoint(a, c1, c2, b, 0.5), cubicTangent(a, c1, c2, b, 0.5)};
}

QRectF nodeAcrossLabelRect(const QString& text, qreal radius, const QFont& font) {
    constexpr qreal kPad = 4.0;
    constexpr qreal kGap = 4.0;
    QFontMetricsF fm(font);
    QRectF textBounds = fm.boundingRect(QRectF(), Qt::TextDontClip, text);
    if (!textBounds.isValid() || textBounds.isEmpty()) {
        const qreal w = fm.horizontalAdvance(text);
        textBounds = QRectF(-w * 0.5, -fm.ascent(), w, fm.height());
    }
    textBounds.moveCenter(QPointF(0.0, -radius - kGap - textBounds.height() * 0.5));
    return textBounds.adjusted(-kPad, -kPad, kPad, kPad);
}

QRectF constantLabelRect(const QPointF& arrowTip, const QPointF& tangent, const QString& text,
                         const QFont& font) {
    constexpr qreal kPad = 6.0;
    constexpr qreal kOffset = 18.0;
    const qreal len = std::hypot(tangent.x(), tangent.y());
    if (len < 1e-6) {
        return {};
    }
    const QPointF u(tangent.x() / len, tangent.y() / len);
    const QPointF n(-u.y(), u.x());
    const QPointF center = arrowTip + n * kOffset;

    const QFontMetricsF fm(font);
    QRectF textBounds = fm.boundingRect(QRectF(), Qt::TextDontClip, text);
    if (!textBounds.isValid() || textBounds.isEmpty()) {
        const qreal w = fm.horizontalAdvance(text);
        textBounds = QRectF(-w * 0.5, -fm.ascent(), w, fm.height());
    }
    textBounds.moveCenter(center);
    return textBounds.adjusted(-kPad, -kPad, kPad, kPad);
}

}  // namespace ci
