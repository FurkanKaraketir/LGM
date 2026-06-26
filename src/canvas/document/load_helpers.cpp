#include "canvas.h"
#include "document_detail.h"

#include "elemental_equation.h"
#include "normal_tree.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <cassert>
#include <vector>

DocVersionStatus classifyDocumentVersion(int version) {
    if (version < 1) {
        return DocVersionStatus::Invalid;
    }
    if (version < kDocumentVersion) {
        return DocVersionStatus::Older;
    }
    if (version > kDocumentVersion) {
        return DocVersionStatus::TooNew;
    }
    return DocVersionStatus::Ok;
}

/*
const bool kDocVersionSelfCheck = [] {
    static_assert(kDocumentVersion == 3, "Self-check expectations assume kDocumentVersion == 3.");
    assert(classifyDocumentVersion(0) == DocVersionStatus::Invalid);
    assert(classifyDocumentVersion(1) == DocVersionStatus::Older);
    assert(classifyDocumentVersion(2) == DocVersionStatus::Older);
    assert(classifyDocumentVersion(3) == DocVersionStatus::Ok);
    assert(classifyDocumentVersion(4) == DocVersionStatus::TooNew);
    return true;
}();
*/

QJsonObject pointToJson(const QPointF& p) {
    return QJsonObject{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
}

QPointF pointFromJson(const QJsonObject& obj) {
    return {obj.value(QStringLiteral("x")).toDouble(), obj.value(QStringLiteral("y")).toDouble()};
}

QJsonObject nodeToJson(const GraphScene::NodeSnapshot& snap) {
    return QJsonObject{{QStringLiteral("pos"), pointToJson(snap.pos)},
                       {QStringLiteral("name"), snap.name},
                       {QStringLiteral("ground"), snap.ground},
                       {QStringLiteral("across"), snap.across},
                       {QStringLiteral("systemType"), static_cast<int>(snap.systemType)}};
}

GraphScene::NodeSnapshot nodeFromJson(const QJsonObject& obj) {
    GraphScene::NodeSnapshot snap;
    snap.pos = pointFromJson(obj.value(QStringLiteral("pos")).toObject());
    snap.name = obj.value(QStringLiteral("name")).toString();
    snap.ground = obj.value(QStringLiteral("ground")).toBool();
    snap.across = obj.value(QStringLiteral("across")).toString();
    snap.systemType = static_cast<SystemType>(obj.value(QStringLiteral("systemType")).toInt());
    return snap;
}

GraphScene::NodeSnapshot nodeSnapshotFromItem(const NodeItem* node) {
    GraphScene::NodeSnapshot snap;
    snap.pos = node->scenePos();
    snap.name = node->name();
    snap.ground = node->isGround();
    snap.across = node->isGround() ? QStringLiteral("0") : node->acrossVariable();
    snap.systemType = node->systemType();
    return snap;
}

QJsonObject branchToJson(const GraphScene::BranchSnapshot& snap, int serialId, int sourceInputId) {
    return QJsonObject{{QStringLiteral("from"), pointToJson(snap.from)},
                       {QStringLiteral("to"), pointToJson(snap.to)},
                       {QStringLiteral("index"), snap.index},
                       {QStringLiteral("name"), snap.name},
                       {QStringLiteral("active"), snap.active},
                       {QStringLiteral("type"), static_cast<int>(snap.type)},
                       {QStringLiteral("constant"), snap.constant},
                       {QStringLiteral("bow"), snap.bow},
                       {QStringLiteral("serialId"), serialId},
                       {QStringLiteral("sourceInputId"), sourceInputId}};
}

GraphScene::BranchSnapshot branchSnapshotFromItem(const BranchItem* branch) {
    GraphScene::BranchSnapshot snap;
    snap.from = branch->from()->scenePos();
    snap.to = branch->to()->scenePos();
    snap.index = branch->index();
    snap.name = branch->name();
    snap.active = branch->isActive();
    snap.type = branch->branchType();
    snap.constant = branch->elementConstant();
    snap.bow = branch->bow();
    return snap;
}

void applyLoadedBranchData(GraphScene* scene, BranchItem* branch, const BranchLoadData& data) {
    if (!scene || !branch) {
        return;
    }
    branch->setBow(data.snap.bow);
    branch->setActive(data.snap.active);
    if (data.serialId > 0) {
        branch->setSerialId(data.serialId);
    }
    if (data.sourceInputId > 0) {
        branch->setSourceInputId(data.sourceInputId);
        scene->registerSourceInputId(data.sourceInputId);
    }
    branch->setElementConstant(data.snap.constant);
    branch->setBranchType(data.snap.type);
    branch->setName(data.snap.name);
}

BranchLoadData branchFromJson(const QJsonObject& obj) {
    BranchLoadData data;
    data.snap.from = pointFromJson(obj.value(QStringLiteral("from")).toObject());
    data.snap.to = pointFromJson(obj.value(QStringLiteral("to")).toObject());
    data.snap.index = obj.value(QStringLiteral("index")).toInt();
    data.snap.name = obj.value(QStringLiteral("name")).toString();
    data.snap.active = obj.value(QStringLiteral("active")).toBool();
    data.snap.type = static_cast<BranchType>(obj.value(QStringLiteral("type")).toInt());
    data.snap.constant = obj.value(QStringLiteral("constant")).toString(QStringLiteral("1"));
    data.snap.bow = obj.value(QStringLiteral("bow")).toDouble();
    data.serialId = obj.value(QStringLiteral("serialId")).toInt();
    data.sourceInputId = obj.value(QStringLiteral("sourceInputId")).toInt();
    return data;
}

QJsonObject twoPortToJson(const TwoPortItem* twoPort) {
    QJsonObject obj{{QStringLiteral("center"), pointToJson(twoPort->center())},
                    {QStringLiteral("kind"), static_cast<int>(twoPort->kind())},
                    {QStringLiteral("modulus"), twoPort->modulus()},
                    {QStringLiteral("name"), twoPort->name()},
                    {QStringLiteral("v1"), nodeToJson(nodeSnapshotFromItem(twoPort->v1()))},
                    {QStringLiteral("v2"), nodeToJson(nodeSnapshotFromItem(twoPort->v2()))},
                    {QStringLiteral("g1"), nodeToJson(nodeSnapshotFromItem(twoPort->g1()))},
                    {QStringLiteral("g2"), nodeToJson(nodeSnapshotFromItem(twoPort->g2()))},
                    {QStringLiteral("leftBranch"),
                     branchToJson(branchSnapshotFromItem(twoPort->leftBranch()),
                                  twoPort->leftBranch()->serialId(),
                                  twoPort->leftBranch()->sourceInputId())},
                    {QStringLiteral("rightBranch"),
                     branchToJson(branchSnapshotFromItem(twoPort->rightBranch()),
                                  twoPort->rightBranch()->serialId(),
                                  twoPort->rightBranch()->sourceInputId())}};
    if (twoPort->hasSharedReference()) {
        obj.insert(QStringLiteral("sharedRef"), true);
    }
    return obj;
}

TwoPortLoadData twoPortFromJson(const QJsonObject& obj) {
    TwoPortLoadData data;
    data.key.center = pointFromJson(obj.value(QStringLiteral("center")).toObject());
    data.key.kind = static_cast<TwoPortKind>(obj.value(QStringLiteral("kind")).toInt());
    data.key.modulus = obj.value(QStringLiteral("modulus")).toString();
    data.key.name = obj.value(QStringLiteral("name")).toString();
    data.v1 = nodeFromJson(obj.value(QStringLiteral("v1")).toObject());
    data.v2 = nodeFromJson(obj.value(QStringLiteral("v2")).toObject());
    data.g1 = nodeFromJson(obj.value(QStringLiteral("g1")).toObject());
    data.g2 = nodeFromJson(obj.value(QStringLiteral("g2")).toObject());
    data.leftBranch = branchFromJson(obj.value(QStringLiteral("leftBranch")).toObject());
    data.rightBranch = branchFromJson(obj.value(QStringLiteral("rightBranch")).toObject());
    data.sharedRef = obj.value(QStringLiteral("sharedRef")).toBool(false);
    return data;
}

QJsonObject savedNormalTreeToJson(const GraphScene::SavedNormalTree& tree) {
    QJsonArray branchIds;
    for (int serialId : tree.treeBranchSerialIds) {
        branchIds.append(serialId);
    }
    return QJsonObject{{QStringLiteral("name"), tree.name},
                       {QStringLiteral("treeBranchSerialIds"), branchIds}};
}

GraphScene::SavedNormalTree savedNormalTreeFromJson(const QJsonObject& obj) {
    GraphScene::SavedNormalTree tree;
    tree.name = obj.value(QStringLiteral("name")).toString();
    for (const QJsonValue& value : obj.value(QStringLiteral("treeBranchSerialIds")).toArray()) {
        tree.treeBranchSerialIds.push_back(value.toInt());
    }
    return tree;
}

bool hasNodeSnapshotAt(const std::vector<GraphScene::NodeSnapshot>& nodes, const QPointF& pos) {
    return std::any_of(nodes.begin(), nodes.end(),
                       [&](const GraphScene::NodeSnapshot& snap) { return snap.pos == pos; });
}

bool isTwoPortPortAt(const GraphScene* scene, const QPointF& pos) {
    for (QGraphicsItem* item : scene->items()) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            if (node->twoPort() && node->scenePos() == pos) {
                return true;
            }
        }
    }
    return false;
}

NodeItem* findNodeAt(const GraphScene* scene, const QPointF& pos) {
    for (QGraphicsItem* item : scene->items()) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            if (node->scenePos() == pos) {
                return node;
            }
        }
    }
    return nullptr;
}

NodeItem* getOrCreateNodeAt(GraphScene* scene, const GraphScene::NodeSnapshot& snap) {
    if (NodeItem* existing = findNodeAt(scene, snap.pos)) {
        return existing;
    }
    if (NodeItem* node = scene->createNode(snap.pos)) {
        node->setName(snap.name);
        node->setGround(snap.ground);
        if (!snap.ground) {
            node->setAcrossVariable(snap.across);
            node->setSystemType(snap.systemType);
        }
        return node;
    }
    return nullptr;
}

GraphScene::NodeSnapshot nodeSnapshotAt(const GraphScene* scene, const QPointF& pos,
                                        SystemType defaultSystemType) {
    GraphScene::NodeSnapshot snap;
    if (NodeItem* node = findNodeAt(scene, pos)) {
        snap.pos = node->scenePos();
        snap.name = node->name();
        snap.ground = node->isGround();
        snap.across = node->isGround() ? QStringLiteral("0") : node->acrossVariable();
        snap.systemType = node->systemType();
        return snap;
    }
    snap.pos = pos;
    snap.name = QStringLiteral("Node");
    snap.ground = false;
    snap.across = QStringLiteral("v");
    snap.systemType = defaultSystemType;
    return snap;
}

void ensureBranchEndpointNodes(std::vector<GraphScene::NodeSnapshot>& nodes, const GraphScene* scene,
                               const QPointF& from, const QPointF& to) {
    for (const QPointF& pos : {from, to}) {
        if (hasNodeSnapshotAt(nodes, pos) || isTwoPortPortAt(scene, pos)) {
            continue;
        }
        nodes.push_back(nodeSnapshotAt(scene, pos, scene->defaultSystemType()));
    }
}

QString endpointKey(const QPointF& pos) {
    return QStringLiteral("%1:%2").arg(pos.x()).arg(pos.y());
}

void ensureSceneNodesForBranchEndpoints(GraphScene* scene, const std::vector<BranchLoadData>& branches) {
    QSet<QString> seen;
    for (const BranchLoadData& data : branches) {
        for (const QPointF& pos : {data.snap.from, data.snap.to}) {
            const QString key = endpointKey(pos);
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            if (!findNodeAt(scene, pos)) {
                scene->createNode(pos);
            }
        }
    }
}

