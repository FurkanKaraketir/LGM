#include "canvas.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <cmath>

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
    if (event->button() != Qt::LeftButton) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    if (m_mode == Mode::AddNode) {
        if (!nodeAt(event->scenePos())) {
            pushAddNode(event->scenePos());
            event->accept();
        }
        return;
    }

    if (m_mode == Mode::AddTwoPort) {
        if (!nodeAt(event->scenePos()) && !twoPortAt(event->scenePos())) {
            pushAddTwoPort(event->scenePos());
            event->accept();
        }
        return;
    }

    if (m_mode == Mode::AddBranch) {
        NodeItem* node = nodeAt(event->scenePos());
        if (!node) {
            event->accept();
            return;
        }

        if (!m_pending) {
            m_pending = node;
            node->setSelected(true);
            event->accept();
            return;
        }

        if (m_pending == node) {
            clearBranchPending();
            event->accept();
            return;
        }

        pushConnectNodes(m_pending, node);
        clearBranchPending();
        event->accept();
        return;
    }

    if (m_pending) {
        clearBranchPending();
    }
    QGraphicsScene::mousePressEvent(event);
}
