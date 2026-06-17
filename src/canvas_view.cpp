#include "canvas.h"

#include <QKeyEvent>
#include <QKeySequence>
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
    case GraphScene::Mode::Delete:
        setCursor(Qt::ForbiddenCursor);
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

void GraphView::keyPressEvent(QKeyEvent* event) {
    auto* graph = static_cast<GraphScene*>(scene());
    if (event->matches(QKeySequence::Undo)) {
        graph->undo();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        graph->redo();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::ZoomIn)) {
        zoomIn();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::ZoomOut)) {
        zoomOut();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_T && graph->mode() == GraphScene::Mode::Select) {
        for (QGraphicsItem* item : graph->selectedItems()) {
            if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
                graph->pushToggleTwoPortKind(twoPort);
                event->accept();
                return;
            }
            if (auto* node = dynamic_cast<NodeItem*>(item)) {
                if (TwoPortItem* twoPort = node->twoPort()) {
                    graph->pushToggleTwoPortKind(twoPort);
                    event->accept();
                    return;
                }
            }
        }
    }
    switch (event->key()) {
    case Qt::Key_N:
        setToolMode(GraphScene::Mode::AddNode);
        event->accept();
        return;
    case Qt::Key_B:
        setToolMode(GraphScene::Mode::AddBranch);
        event->accept();
        return;
    case Qt::Key_P:
        setToolMode(GraphScene::Mode::AddTwoPort);
        event->accept();
        return;
    case Qt::Key_D:
        setToolMode(GraphScene::Mode::Delete);
        event->accept();
        return;
    case Qt::Key_Escape:
        setToolMode(GraphScene::Mode::Select);
        event->accept();
        return;
    default:
        break;
    }
    QGraphicsView::keyPressEvent(event);
}
