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

namespace {

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

constexpr QColor kSelectionColor(0, 100, 200);
constexpr QColor kNormalTreeColor(27, 94, 32);
constexpr QColor kCotreeColor(158, 158, 158);

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

constexpr qreal kTfHalfHeight = 14.0;

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
    const qreal midY = (left.y() + right.y()) / 2.0;
    const qreal padX = 8.0;
    const QRectF box(left.x() - padX, midY - kTfHalfHeight, right.x() - left.x() + padX * 2.0,
                     kTfHalfHeight * 2.0);
    painter->setBrush(fill);
    painter->drawPath(stadiumPath(box));

    const qreal span = right.x() - left.x();
    drawDownArrow(painter, QPointF(left.x(), midY - 6.0));
    drawDownArrow(painter, QPointF(right.x(), midY - 6.0));
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

qreal externalBranchLane(int index, int count) {
    return (count <= 1) ? 0.0 : kLaneSpread * 0.55 * (index - (count - 1) / 2.0);
}

QPointF twoPortKick(const TwoPortItem* tp, const NodeItem* port, qreal lane, qreal chordLen) {
    if (!tp || !port) {
        return {};
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
        egress.kickA = twoPortKick(from->twoPort(), from, externalBranchLane(index, count), chordLen);
    }
    if (NodeItem* to = branch->to(); to && to->twoPort()) {
        int index = 0;
        int count = 1;
        externalLaneAtPort(to, branch, index, count);
        egress.kickB = twoPortKick(to->twoPort(), to, externalBranchLane(index, count), chordLen);
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

}  // namespace

BranchItem::BranchItem(NodeItem* from, NodeItem* to, int index, int count, qreal bow)
    : m_from(from), m_to(to), m_index(index), m_count(count), m_bow(bow) {
    setBrush(Qt::NoBrush);
    setFlags(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemClipsToShape, false);
    setZValue(0);
    refreshTheme();
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
    prepareGeometryChange();
    const QPointF a = m_from->scenePos();
    const QPointF b = m_to->scenePos();
    const bool dashedTail = !m_active && m_type == BranchType::A;
    const BranchEgress egress = computeBranchEgress(this);
    const bool drawArrow = !isTwoPortPortBranch(this);
    if (std::abs(m_bow) > 1e-6) {
        setPath(bowedBranch(a, b, m_bow, m_active, dashedTail, drawArrow));
    } else {
        setPath(parallelBranch(a, b, m_index, m_count, egress.kickA, egress.kickB, m_active, dashedTail,
                                drawArrow));
    }
}

void BranchItem::setActive(bool active) {
    m_active = active;
    if (active && m_type == BranchType::D) {
        m_type = BranchType::A;
    }
    if (active && (m_type == BranchType::A || m_type == BranchType::T) && m_sourceInputId == 0) {
        const int parsed = lg::parseSourceInputIdFromName(m_name);
        if (parsed > 0) {
            m_sourceInputId = parsed;
        } else if (auto* graphScene = qobject_cast<GraphScene*>(scene())) {
            m_sourceInputId = graphScene->allocateSourceInputId();
        } else {
            m_sourceInputId = 1;
        }
        if (auto* graphScene = qobject_cast<GraphScene*>(scene())) {
            graphScene->registerSourceInputId(m_sourceInputId);
        }
    }
    lg::applySourceThroughNaming(this);
    updatePath();
}

void BranchItem::setBranchType(BranchType type) {
    m_type = type;
    lg::applySourceThroughNaming(this);
    updatePath();
}

void BranchItem::flip() {
    std::swap(m_from, m_to);
    m_bow = -m_bow;
    updatePath();
}

void BranchItem::replaceEndpoint(NodeItem* oldNode, NodeItem* newNode) {
    if (!oldNode || !newNode || oldNode == newNode) {
        return;
    }
    if (m_from == oldNode) {
        oldNode->removeBranch(this);
        m_from = newNode;
        newNode->addBranch(this);
    }
    if (m_to == oldNode) {
        oldNode->removeBranch(this);
        m_to = newNode;
        newNode->addBranch(this);
    }
    updatePath();
}

QString BranchItem::elementalEquationText() const {
    return lg::elementalEquationText(m_type, lg::branchSystemType(*this), !m_active, m_elementConstant);
}

void BranchItem::setElementConstant(const QString& constant) {
    if (m_elementConstant == constant) {
        return;
    }
    prepareGeometryChange();
    m_elementConstant = constant;
    if (!m_active) {
        if (const std::optional<BranchType> inferred =
                lg::inferPassiveBranchType(constant, lg::branchSystemType(*this))) {
            m_type = *inferred;
        }
    }
    lg::applyConstantThroughNaming(this);
    update();
}

void BranchItem::setNormalTreeRole(bool inTree, bool known) {
    m_inNormalTree = inTree;
    m_normalTreeRoleKnown = known;
    updateBranchPen();
}

void BranchItem::updateBranchPen() {
    QPen pen;
    if (isSelected()) {
        pen = QPen(kSelectionColor, 2);
    } else if (m_normalTreeRoleKnown && m_inNormalTree) {
        pen = QPen(kNormalTreeColor, 3);
    } else if (m_normalTreeRoleKnown) {
        pen = QPen(kCotreeColor, 1.5);
    } else {
        pen = QPen(itemInk(nullptr), 2);
    }
    setPen(pen);
}

void BranchItem::refreshTheme() {
    updateBranchPen();
    update();
}

QRectF BranchItem::boundingRect() const {
    QRectF rect = QGraphicsPathItem::boundingRect();
    if (!m_from || !m_to || !m_from->scene() || !m_to->scene()) {
        return rect;
    }
    const QPointF a = m_from->scenePos();
    const QPointF b = m_to->scenePos();
    const BranchEgress egress = computeBranchEgress(this);
    const BranchArrowGeom arrow =
        branchArrowGeom(a, b, m_index, m_count, m_bow, egress.kickA, egress.kickB);
    const QString label = branchAnnotationLabel(this);
    QFont font;
    font.setPointSizeF(9.0);
    rect |= constantLabelRect(arrow.tip, arrow.tangent, label, font);
    return rect.marginsAdded(QMarginsF(2.0, 2.0, 2.0, 2.0));
}

QPainterPath BranchItem::shape() const {
    return strokedPickShape(path());
}

void BranchItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    QGraphicsPathItem::paint(painter, option, widget);

    if (!m_from || !m_to || !m_from->scene() || !m_to->scene()) {
        return;
    }
    if (isTwoPortPortBranch(this)) {
        return;
    }

    const QPointF a = m_from->scenePos();
    const QPointF b = m_to->scenePos();
    const BranchEgress egress = computeBranchEgress(this);
    const BranchArrowGeom arrow =
        branchArrowGeom(a, b, m_index, m_count, m_bow, egress.kickA, egress.kickB);

    painter->save();
    painter->setClipping(false);
    if (!m_active) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(pen().color());
        painter->fillPath(filledArrowHead(arrow.tip, arrow.tangent), pen().color());
    }
    painter->restore();

    const QString label = branchAnnotationLabel(this);
    QFont font = painter->font();
    font.setPointSizeF(9.0);
    painter->setFont(font);
    painter->setPen(isSelected() ? kSelectionColor : itemInk(widget));

    const QRectF textRect = constantLabelRect(arrow.tip, arrow.tangent, label, font);
    painter->save();
    painter->setClipping(false);
    painter->drawText(textRect, Qt::AlignCenter, label);
    painter->restore();
}

void BranchItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (!shape().contains(event->pos())) {
        event->ignore();
        return;
    }
    if (scene() && event->button() == Qt::LeftButton) {
        auto* graph = static_cast<GraphScene*>(scene());
        if (graph->mode() == GraphScene::Mode::SelectNormalTree) {
            graph->toggleManualNormalTreeBranch(this);
            event->accept();
            return;
        }
    }
    QGraphicsPathItem::mousePressEvent(event);
}

void BranchItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (!scene()) {
        return;
    }
    if (static_cast<GraphScene*>(scene())->mode() == GraphScene::Mode::SelectNormalTree) {
        event->accept();
        return;
    }
    if (isTwoPortPortBranch(this)) {
        event->accept();
        return;
    }
    auto* dialog = new QDialog(scene()->views().first());
    dialog->setWindowTitle("Branch Properties");
    auto* layout = new QVBoxLayout(dialog);
    
    layout->addWidget(new QLabel("Branch Category:"));
    auto* categoryCombo = new QComboBox();
    categoryCombo->addItem("Passive Element", false);
    categoryCombo->addItem("Active Element (Source)", true);
    categoryCombo->setCurrentIndex(m_active ? 1 : 0);
    layout->addWidget(categoryCombo);
    
    auto* typeLabel = new QLabel("Element Type:");
    layout->addWidget(typeLabel);
    auto* typeCombo = new QComboBox();
    layout->addWidget(typeCombo);
    
    auto updateTypeOptions = [=]() {
        const bool isActive = categoryCombo->currentData().toBool();
        typeCombo->clear();
        if (isActive) {
            typeCombo->addItem("A-type", static_cast<int>(BranchType::A));
            typeCombo->addItem("T-type", static_cast<int>(BranchType::T));
            int currentIdx = (m_type == BranchType::T) ? 1 : 0;
            typeCombo->setCurrentIndex(currentIdx);
        } else {
            typeCombo->addItem("A-type", static_cast<int>(BranchType::A));
            typeCombo->addItem("T-type", static_cast<int>(BranchType::T));
            typeCombo->addItem("D-type", static_cast<int>(BranchType::D));
            int currentIdx = 0;
            switch (m_type) {
                case BranchType::A: currentIdx = 0; break;
                case BranchType::T: currentIdx = 1; break;
                case BranchType::D: currentIdx = 2; break;
            }
            typeCombo->setCurrentIndex(currentIdx);
        }
    };
    
    QObject::connect(categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateTypeOptions);
    updateTypeOptions();

    auto* constantLabel = new QLabel("Constant:");
    auto* constantEdit = new QLineEdit(m_elementConstant);
    constantEdit->setPlaceholderText(QStringLiteral("e.g. R1, C2, M3"));
    layout->addWidget(constantLabel);
    layout->addWidget(constantEdit);

    auto refreshConstantVisibility = [=]() {
        const bool passive = !categoryCombo->currentData().toBool();
        constantLabel->setVisible(passive);
        constantEdit->setVisible(passive);
    };
    QObject::connect(categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), refreshConstantVisibility);
    QObject::connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), refreshConstantVisibility);
    refreshConstantVisibility();
    
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=]() {
        if (!categoryCombo->currentData().toBool() &&
            !lg::isValidElementConstant(constantEdit->text())) {
            constantEdit->setFocus();
            constantEdit->selectAll();
            return;
        }
        dialog->accept();
    });

    if (dialog->exec() == QDialog::Accepted) {
        auto* graph = static_cast<GraphScene*>(scene());
        graph->pushBranchProperties(
            this, categoryCombo->currentData().toBool(),
            static_cast<BranchType>(typeCombo->currentData().toInt()),
            constantEdit->text());
    }
    dialog->deleteLater();
    event->accept();
}

QVariant BranchItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemSelectedHasChanged) {
        updateBranchPen();
    }
    return QGraphicsPathItem::itemChange(change, value);
}

NodeItem::NodeItem(qreal radius) : QGraphicsEllipseItem(-radius, -radius, radius * 2, radius * 2) {
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemSendsGeometryChanges);
    setZValue(1);
    refreshTheme();
}

void NodeItem::refreshTheme() {
    setBrush(itemFill(nullptr));
    setPen(QPen(itemInk(nullptr), 2));
    update();
}

void NodeItem::setAcrossVariable(const QString& symbol) {
    if (m_acrossVariable == symbol) {
        return;
    }
    prepareGeometryChange();
    m_acrossVariable = symbol;
    update();
}

void NodeItem::setGround(bool ground) {
    prepareGeometryChange();
    m_ground = ground;
    update();
}

void NodeItem::setSystemType(SystemType type) {
    if (m_systemType == type) {
        return;
    }
    m_systemType = type;
    lg::applyNodeDomainNaming(this);
    update();
}

void NodeItem::setTwoPort(TwoPortItem* twoPort) {
    m_twoPort = twoPort;
    setZValue(twoPort ? 3.0 : 1.0);
}

QRectF NodeItem::boundingRect() const {
    QRectF r = QGraphicsEllipseItem::boundingRect();
    if (m_ground) {
        // ground symbol extends: radius+2 stem + 8 down + 14 for three rungs = radius+16 below center
        const qreal extra = rect().width() / 2.0 + 18.0;
        r.setBottom(r.bottom() + extra);
    }
    QFont font;
    font.setPointSizeF(9.0);
    r |= nodeAcrossLabelRect(acrossVariable(), rect().width() / 2.0, font);
    return r;
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    QGraphicsEllipseItem::paint(painter, option, widget);
    if (m_ground) {
        painter->save();
        painter->setPen(QPen(itemInk(widget), 1.5));
        painter->setBrush(Qt::NoBrush);
        drawGroundSymbol(painter, QPointF(0.0, 0.0), rect().width() / 2.0);
        painter->restore();
    }
    const qreal radius = rect().width() / 2.0;
    const QString label = acrossVariable();
    QFont font = painter->font();
    font.setPointSizeF(9.0);
    painter->setFont(font);
    painter->setPen(isSelected() ? kSelectionColor : itemInk(widget));
    const QRectF textRect = nodeAcrossLabelRect(label, radius, font);
    painter->save();
    painter->setClipping(false);
    painter->drawText(textRect, Qt::AlignCenter, label);
    painter->restore();
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
    if (change == ItemSelectedHasChanged) {
        update();
    }
    if (change == ItemPositionChange && scene()) {
        const auto* graph = static_cast<GraphScene*>(scene());
        return graph->snap(value.toPointF());
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
        if (graph->mode() == GraphScene::Mode::SelectNormalTree) {
            event->accept();
            return;
        }
        if (graph->mode() == GraphScene::Mode::Select) {
            if (!(event->modifiers() & Qt::ShiftModifier)) {
                graph->selectTwoPortNode(this);
            } else {
                setSelected(true);
            }
            event->accept();
            return;
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
        graph->tryMergeOverlappingNodes(this);
    }
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (!scene() || m_twoPort) {
        QGraphicsEllipseItem::mouseDoubleClickEvent(event);
        return;
    }
    if (static_cast<GraphScene*>(scene())->mode() == GraphScene::Mode::SelectNormalTree) {
        event->accept();
        return;
    }
    auto* dialog = new QDialog(scene()->views().first());
    dialog->setWindowTitle("Node Properties");
    auto* layout = new QVBoxLayout(dialog);
    
    layout->addWidget(new QLabel("Node Type:"));
    auto* normalRadio = new QRadioButton("Normal Node");
    auto* groundRadio = new QRadioButton("Reference (0-Ground) Node");
    normalRadio->setChecked(!m_ground);
    groundRadio->setChecked(m_ground);
    layout->addWidget(normalRadio);
    layout->addWidget(groundRadio);

    QComboBox* systemTypeCombo = nullptr;
    if (!m_ground) {
        layout->addWidget(new QLabel("System Type:"));
        systemTypeCombo = new QComboBox();
        systemTypeCombo->addItem("Mechanical (Translational)", static_cast<int>(SystemType::Mechanical));
        systemTypeCombo->addItem("Mechanical (Rotational)", static_cast<int>(SystemType::MechanicalRotational));
        systemTypeCombo->addItem("Electrical", static_cast<int>(SystemType::Electrical));
        systemTypeCombo->addItem("Fluid", static_cast<int>(SystemType::Fluid));
        systemTypeCombo->addItem("Heat", static_cast<int>(SystemType::Heat));
        systemTypeCombo->setCurrentIndex(systemTypeCombo->findData(static_cast<int>(m_systemType)));
        layout->addWidget(systemTypeCombo);
    }
    
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    
    if (dialog->exec() == QDialog::Accepted) {
        auto* graph = static_cast<GraphScene*>(scene());
        const bool newGround = groundRadio->isChecked();
        const SystemType newSystemType =
            systemTypeCombo && !newGround
                ? static_cast<SystemType>(systemTypeCombo->currentData().toInt())
                : m_systemType;
        graph->pushNodeProperties(this, m_ground, newGround, m_systemType, newSystemType);
    }
    dialog->deleteLater();
    event->accept();
}

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
    if (m_kind == TwoPortKind::Gyrator) {
        m_left->setBow(14.0);
        m_right->setBow(-14.0);
    } else {
        m_left->setBow(-14.0);
        m_right->setBow(14.0);
    }
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
    return lg::twoPortElementalEquationText(m_kind, m_modulus);
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
    return stadiumPath(QRectF(left.x() - 8.0, midY - kTfHalfHeight, span + 16.0, kTfHalfHeight * 2.0));
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
