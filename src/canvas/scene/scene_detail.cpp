#include "scene_detail.h"

#include "canvas.h"

void removeFromScene(GraphScene* scene, QGraphicsItem* item) {
    if (item && item->scene() == scene) {
        scene->removeItem(item);
    }
}

void refreshTwoPortEgressAt(NodeItem* port) {
    if (!port || !port->twoPort()) {
        return;
    }
    TwoPortItem* twoPort = port->twoPort();
    twoPort->refresh();
    for (BranchItem* branch : port->branches()) {
        if (!GraphScene::isInternalTwoPortBranch(twoPort, branch)) {
            branch->updatePath();
        }
    }
}
