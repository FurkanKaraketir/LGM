#include "tool_icons.h"

#include <QApplication>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <functional>

namespace {

using DrawFn = std::function<void(QPainter&, const QRectF&, const QColor&, int size)>;

QIcon singleColorIcon(const DrawFn& draw) {
    QIcon icon;
    const QPalette& palette = QApplication::palette();
    const QColor normal = palette.color(QPalette::WindowText);
    const QColor disabled = palette.color(QPalette::Disabled, QPalette::WindowText);

    for (const int size : {16, 24, 32}) {
        const QRectF rect(0, 0, size, size);

        auto addPixmap = [&](const QColor& color, QIcon::Mode mode) {
            QPixmap pixmap(size, size);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            draw(painter, rect, color, size);
            icon.addPixmap(pixmap, mode);
        };

        addPixmap(normal, QIcon::Normal);
        addPixmap(normal, QIcon::Active);
        addPixmap(disabled, QIcon::Disabled);
    }

    return icon;
}

void drawNode(QPainter& painter, const QRectF& rect, const QColor& color, int size) {
    const qreal inset = size * 0.22;
    const QPen pen(color, std::max(1.0, size / 12.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(rect.adjusted(inset, inset, -inset, -inset));
}

void drawBranch(QPainter& painter, const QRectF& rect, const QColor& color, int size) {
    const qreal inset = size * 0.18;
    const QPen pen(color, std::max(1.0, size / 12.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QPointF start(rect.left() + inset, rect.bottom() - inset);
    const QPointF end(rect.right() - inset, rect.bottom() - inset);
    const QPointF control(rect.center().x(), rect.top() + inset);

    QPainterPath path;
    path.moveTo(start);
    path.quadTo(control, end);
    painter.drawPath(path);
}

void drawBowedBranch(QPainter& painter, const QPointF& from, const QPointF& to, qreal bow) {
    QPainterPath path;
    path.moveTo(from);
    path.quadTo((from + to) / 2.0 + QPointF(bow, 0.0), to);
    painter.drawPath(path);
}

void drawNodeAt(QPainter& painter, const QPointF& center, qreal radius) {
    painter.drawEllipse(center, radius, radius);
}

void drawTwoPort(QPainter& painter, const QRectF& rect, const QColor& color, int size) {
    const qreal inset = size * 0.16;
    const qreal nodeR = size * 0.085;
    const qreal bow = size * 0.14;
    const QPen pen(color, std::max(1.0, size / 12.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QPointF v1(rect.left() + inset, rect.top() + inset);
    const QPointF v2(rect.right() - inset, rect.top() + inset);
    const QPointF g1(rect.left() + inset, rect.bottom() - inset);
    const QPointF g2(rect.right() - inset, rect.bottom() - inset);

    drawBowedBranch(painter, v1, g1, bow);
    drawBowedBranch(painter, v2, g2, -bow);

    for (const QPointF& node : {v1, v2, g1, g2}) {
        drawNodeAt(painter, node, nodeR);
    }
}

void drawAnalyze(QPainter& painter, const QRectF& rect, const QColor& color, int size) {
    const qreal inset = size * 0.18;
    const qreal nodeR = size * 0.08;
    const QPen pen(color, std::max(1.0, size / 12.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QPointF root(rect.center().x(), rect.top() + inset);
    const QPointF left(rect.left() + inset, rect.bottom() - inset);
    const QPointF right(rect.right() - inset, rect.bottom() - inset);

    painter.drawLine(root, left);
    painter.drawLine(root, right);
    for (const QPointF& node : {root, left, right}) {
        drawNodeAt(painter, node, nodeR);
    }
}

}  // namespace

QIcon ToolIcons::select() {
    return QIcon(QStringLiteral(":/select_icon.png"));
}

QIcon ToolIcons::node() {
    return singleColorIcon(drawNode);
}

QIcon ToolIcons::branch() {
    return singleColorIcon(drawBranch);
}

QIcon ToolIcons::twoPort() {
    return singleColorIcon(drawTwoPort);
}

QIcon ToolIcons::analyze() {
    return singleColorIcon(drawAnalyze);
}
