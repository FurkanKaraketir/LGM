#include "canvas.h"

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <cmath>

void GraphScene::drawBackground(QPainter* painter, const QRectF& rect) {
    const QPalette pal =
        views().isEmpty() ? QApplication::palette() : views().constFirst()->palette();
    painter->fillRect(rect, pal.color(QPalette::Window));
    if (!m_showGrid) {
        return;
    }

    const qreal left = std::floor(rect.left() / m_gridSpacing) * m_gridSpacing;
    const qreal top = std::floor(rect.top() / m_gridSpacing) * m_gridSpacing;

    painter->setPen(pal.color(QPalette::Mid));
    for (qreal x = left; x < rect.right(); x += m_gridSpacing) {
        for (qreal y = top; y < rect.bottom(); y += m_gridSpacing) {
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
