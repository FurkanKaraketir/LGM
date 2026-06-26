#pragma once

class GraphScene;
class NodeItem;
class QGraphicsItem;

void removeFromScene(GraphScene* scene, QGraphicsItem* item);
void refreshTwoPortEgressAt(NodeItem* port);
