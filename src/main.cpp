#include "canvas.h"

#include <QApplication>
#include <QGraphicsView>
#include <QMainWindow>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    auto* scene = new GraphScene;
    NodeItem* left = scene->addNode({0, 0});
    NodeItem* mid = scene->addNode({240, 0});
    NodeItem* right = scene->addNode({480, 0});
    NodeItem* bottom = scene->addNode({240, 300});

    scene->connectNodes(left, mid);
    scene->connectNodes(mid, right);
    scene->connectNodes(left, bottom);
    scene->connectNodes(mid, bottom);
    scene->connectNodes(right, bottom);

    auto* view = new QGraphicsView(scene);
    view->setRenderHint(QPainter::Antialiasing);
    view->setDragMode(QGraphicsView::RubberBandDrag);
    view->setWindowTitle("Linear Graph Modeling");
    view->resize(900, 700);
    view->show();

    return app.exec();
}
