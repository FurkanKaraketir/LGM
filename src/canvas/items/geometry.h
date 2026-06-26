#pragma once

#include "canvas.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QString>

class BranchItem;
class NodeItem;
class TwoPortItem;

namespace ci {

inline const QColor kSelectionColor(0, 100, 200);
inline const QColor kNormalTreeColor(27, 94, 32);
inline const QColor kCotreeColor(158, 158, 158);

constexpr qreal kTfPadX = 2.0;
constexpr qreal kTfHalfHeight = 6.0;

QPainterPath strokedPickShape(const QPainterPath& source, qreal width = 6.0);
QColor itemInk(const QWidget* widget);
QColor itemFill(const QWidget* widget);
void appendArrowHead(QPainterPath& path, const QPointF& tip, const QPointF& tangent, qreal size = 9.0,
                     bool withCircle = false);
QPainterPath filledArrowHead(const QPointF& tip, const QPointF& tangent, qreal size = 8.0);
void drawDownArrow(QPainter* painter, const QPointF& top, qreal stemLen = 11.0);
QPainterPath stadiumPath(const QRectF& rect);
QPainterPath parallelBranch(const QPointF& a, const QPointF& b, int index, int count,
                            const QPointF& kickA = {}, const QPointF& kickB = {}, bool active = false,
                            bool dashedTail = false, bool drawArrow = true);
QPainterPath bowedBranch(const QPointF& top, const QPointF& bottom, qreal bow, bool active = false,
                         bool dashedTail = false, bool drawArrow = true);
void drawGroundSymbol(QPainter* painter, const QPointF& center, qreal radius);
void drawTransformerCoupler(QPainter* painter, const QPointF& left, const QPointF& right,
                            const QString& modulus, const QColor& fill);
void drawGyratorCoupler(QPainter* painter, const QPointF& left, const QPointF& right,
                        const QString& modulus);
QPainterPath gyratorInfinityPath(const QPointF& left, const QPointF& right);
void refreshNeighborTwoPortBows(TwoPortItem* tp);

struct BranchArrowGeom {
    QPointF tip;
    QPointF tangent;
};

bool isTwoPortPortBranch(const BranchItem* branch);
qreal twoPortSideBowMag(const TwoPortItem* tp, bool leftSide);
void externalLaneAtPort(const NodeItem* port, const BranchItem* branch, int& index, int& count);

struct BranchEgress {
    QPointF kickA;
    QPointF kickB;
};

BranchEgress computeBranchEgress(const BranchItem* branch);
QString branchAnnotationLabel(const BranchItem* branch);
BranchArrowGeom branchArrowGeom(const QPointF& a, const QPointF& b, int index, int count, qreal bow,
                                const QPointF& kickA = {}, const QPointF& kickB = {});
QRectF nodeAcrossLabelRect(const QString& text, qreal radius, const QFont& font);
QRectF constantLabelRect(const QPointF& arrowTip, const QPointF& tangent, const QString& text,
                         const QFont& font);

}  // namespace ci
