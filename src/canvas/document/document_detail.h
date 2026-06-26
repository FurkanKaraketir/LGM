#pragma once

#include "canvas.h"

#include <QJsonObject>
#include <QPointF>
#include <vector>

constexpr int kDocumentVersion = 3;

enum class DocVersionStatus { Ok, Older, TooNew, Invalid };

DocVersionStatus classifyDocumentVersion(int version);

QJsonObject pointToJson(const QPointF& p);
QPointF pointFromJson(const QJsonObject& obj);
QJsonObject nodeToJson(const GraphScene::NodeSnapshot& snap);
GraphScene::NodeSnapshot nodeFromJson(const QJsonObject& obj);
QJsonObject branchToJson(const GraphScene::BranchSnapshot& snap, int serialId, int sourceInputId);

struct BranchLoadData {
    GraphScene::BranchSnapshot snap;
    int serialId = 0;
    int sourceInputId = 0;
};

BranchLoadData branchFromJson(const QJsonObject& obj);
void applyLoadedBranchData(GraphScene* scene, BranchItem* branch, const BranchLoadData& data);

QJsonObject twoPortToJson(const TwoPortItem* twoPort);

struct TwoPortLoadData {
    GraphScene::TwoPortKey key;
    GraphScene::NodeSnapshot v1;
    GraphScene::NodeSnapshot v2;
    GraphScene::NodeSnapshot g1;
    GraphScene::NodeSnapshot g2;
    BranchLoadData leftBranch;
    BranchLoadData rightBranch;
    bool sharedRef = false;
};

TwoPortLoadData twoPortFromJson(const QJsonObject& obj);
QJsonObject savedNormalTreeToJson(const GraphScene::SavedNormalTree& tree);
GraphScene::SavedNormalTree savedNormalTreeFromJson(const QJsonObject& obj);

bool hasNodeSnapshotAt(const std::vector<GraphScene::NodeSnapshot>& nodes, const QPointF& pos);
bool isTwoPortPortAt(const GraphScene* scene, const QPointF& pos);
void ensureBranchEndpointNodes(std::vector<GraphScene::NodeSnapshot>& nodes, const GraphScene* scene,
                               const QPointF& from, const QPointF& to);
void ensureSceneNodesForBranchEndpoints(GraphScene* scene, const std::vector<BranchLoadData>& branches);
NodeItem* findNodeAt(const GraphScene* scene, const QPointF& pos);
NodeItem* getOrCreateNodeAt(GraphScene* scene, const GraphScene::NodeSnapshot& snap);
