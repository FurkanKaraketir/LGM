#include "canvas.h"

#include <QWheelEvent>

GraphView::GraphView(GraphScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    setFocusPolicy(Qt::StrongFocus);
    setToolMode(GraphScene::Mode::Select);
}

void GraphView::setToolMode(GraphScene::Mode mode) {
    auto* graph = static_cast<GraphScene*>(scene());
    graph->setMode(mode);
    switch (mode) {
    case GraphScene::Mode::Select:
        setCursor(Qt::ArrowCursor);
        setDragMode(QGraphicsView::RubberBandDrag);
        break;
    case GraphScene::Mode::AddNode:
        setCursor(Qt::CrossCursor);
        setDragMode(QGraphicsView::NoDrag);
        break;
    case GraphScene::Mode::AddBranch:
    case GraphScene::Mode::AddTwoPort:
        setCursor(Qt::PointingHandCursor);
        setDragMode(QGraphicsView::NoDrag);
        break;
    case GraphScene::Mode::SelectNormalTree:
        setCursor(Qt::PointingHandCursor);
        setDragMode(QGraphicsView::NoDrag);
        break;
    }
}

void GraphView::goHome() {
    auto* graph = static_cast<GraphScene*>(scene());
    const QRectF bounds = graph->contentBounds();
    if (bounds.isEmpty()) {
        resetTransform();
        centerOn(0, 0);
        return;
    }
    fitInView(bounds.marginsAdded(QMarginsF(80, 80, 80, 80)), Qt::KeepAspectRatio);
}

void GraphView::zoomBy(qreal factor, const QPoint& anchor) {
    const qreal scale = transform().m11() * factor;
    constexpr qreal kMinScale = 0.05;
    constexpr qreal kMaxScale = 20.0;
    if (scale < kMinScale || scale > kMaxScale) {
        return;
    }

    const QPoint center = anchor.isNull() ? viewport()->rect().center() : anchor;
    const QPointF sceneAnchor = mapToScene(center);
    QGraphicsView::scale(factor, factor);
    const QPointF delta = mapToScene(center) - sceneAnchor;
    translate(delta.x(), delta.y());
}

void GraphView::zoomIn() {
    zoomBy(1.2, {});
}

void GraphView::zoomOut() {
    zoomBy(1.0 / 1.2, {});
}

void GraphView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ShiftModifier) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            const qreal factor = delta > 0 ? 1.15 : 1.0 / 1.15;
            zoomBy(factor, event->position().toPoint());
        }
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}
