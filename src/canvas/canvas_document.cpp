#include "canvas.h"

#include "elemental_equation.h"
#include "normal_tree.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <cassert>
#include <vector>

namespace {

constexpr int kDocumentVersion = 3;

enum class DocVersionStatus { Ok, Older, TooNew, Invalid };

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

struct BranchLoadData {
    GraphScene::BranchSnapshot snap;
    int serialId = 0;
    int sourceInputId = 0;
};

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

}  // namespace

void GraphScene::clearDocument() {
    const bool blocked = blockSignals(true);
    m_suppressGraphChange = true;

    m_undoStack.clear();
    clearBranchPending();
    clearSelection();

    m_normalTreeHighlightActive = false;
    m_normalTreeManual = false;
    m_lastNormalTreeResult = {};
    m_lastStateSpaceResult = {};
    m_savedNormalTrees.clear();
    m_activeSavedNormalTreeIndex = -1;
    for (QGraphicsItem* item : items()) {
        if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branch->setNormalTreeRole(false, false);
        }
    }

    bool destroyed = true;
    while (destroyed) {
        destroyed = false;
        for (QGraphicsItem* item : items()) {
            if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
                destroyTwoPort(twoPort);
                destroyed = true;
                break;
            }
        }
        if (destroyed) {
            continue;
        }
        for (QGraphicsItem* item : items()) {
            if (auto* branch = dynamic_cast<BranchItem*>(item)) {
                destroyBranch(branch);
                destroyed = true;
                break;
            }
        }
        if (destroyed) {
            continue;
        }
        for (QGraphicsItem* item : items()) {
            if (auto* node = dynamic_cast<NodeItem*>(item)) {
                destroyNode(node);
                destroyed = true;
                break;
            }
        }
    }

    m_suppressGraphChange = false;
    blockSignals(blocked);

    m_nextNodeId = 1;
    m_nextBranchId = 1;
    m_nextSourceInputId = 1;
    m_nextTwoPortId = 1;
    notifyGraphChanged();
}

QByteArray GraphScene::documentToJson() const {
    std::vector<NodeSnapshot> nodes;
    struct BranchRecord {
        BranchSnapshot snap;
        int serialId = 0;
        int sourceInputId = 0;
    };
    std::vector<BranchRecord> branches;
    for (QGraphicsItem* item : items()) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            if (node->twoPort()) {
                continue;
            }
            NodeSnapshot snap;
            snap.pos = node->scenePos();
            snap.name = node->name();
            snap.ground = node->isGround();
            snap.across = node->isGround() ? QStringLiteral("0") : node->acrossVariable();
            snap.systemType = node->systemType();
            nodes.push_back(snap);
        } else if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            if (TwoPortItem* twoPort = twoPortFor(branch)) {
                if (isInternalTwoPortBranch(twoPort, branch)) {
                    continue;
                }
            }
            BranchRecord record;
            record.snap.from = branch->from()->scenePos();
            record.snap.to = branch->to()->scenePos();
            record.snap.index = branch->index();
            record.snap.name = branch->name();
            record.snap.active = branch->isActive();
            record.snap.type = branch->branchType();
            record.snap.constant = branch->elementConstant();
            record.snap.bow = branch->bow();
            record.serialId = branch->serialId();
            record.sourceInputId = branch->sourceInputId();
            branches.push_back(record);
        }
    }

    for (const BranchRecord& record : branches) {
        ensureBranchEndpointNodes(nodes, this, record.snap.from, record.snap.to);
    }

    QJsonArray twoPortArray;
    for (QGraphicsItem* item : items()) {
        if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            twoPortArray.append(twoPortToJson(twoPort));
        }
    }

    QJsonArray nodeArray;
    for (const NodeSnapshot& snap : nodes) {
        nodeArray.append(nodeToJson(snap));
    }

    QJsonArray branchArray;
    for (const BranchRecord& record : branches) {
        branchArray.append(branchToJson(record.snap, record.serialId, record.sourceInputId));
    }

    QJsonArray normalTreeArray;
    for (const SavedNormalTree& tree : m_savedNormalTrees) {
        normalTreeArray.append(savedNormalTreeToJson(tree));
    }

    QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("LGM")},
        {QStringLiteral("version"), kDocumentVersion},
        {QStringLiteral("settings"),
         QJsonObject{{QStringLiteral("defaultSystemType"), static_cast<int>(m_defaultSystemType)},
                     {QStringLiteral("snapToGrid"), m_snapToGrid},
                     {QStringLiteral("showGrid"), m_showGrid},
                     {QStringLiteral("gridSpacing"), m_gridSpacing},
                     {QStringLiteral("nextNodeId"), m_nextNodeId},
                     {QStringLiteral("nextBranchId"), m_nextBranchId},
                     {QStringLiteral("nextSourceInputId"), m_nextSourceInputId},
                     {QStringLiteral("nextTwoPortId"), m_nextTwoPortId}}},
        {QStringLiteral("twoPorts"), twoPortArray},
        {QStringLiteral("nodes"), nodeArray},
        {QStringLiteral("branches"), branchArray},
        {QStringLiteral("normalTrees"), normalTreeArray},
    };
    if (m_activeSavedNormalTreeIndex >= 0) {
        root.insert(QStringLiteral("activeNormalTreeIndex"), m_activeSavedNormalTreeIndex);
    }

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool GraphScene::documentFromJson(const QByteArray& data, QString* error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = tr("Invalid JSON: %1").arg(parseError.errorString());
        }
        return false;
    }
    if (!doc.isObject()) {
        if (error) {
            *error = tr("Document root must be a JSON object.");
        }
        return false;
    }

    const QJsonObject root = doc.object();
    const QString format = root.value(QStringLiteral("format")).toString();
    if (format != QStringLiteral("LGM") && format != QStringLiteral("LinearGraphModeling")) {
        if (error) {
            *error = tr("Not an LGM document.");
        }
        return false;
    }
    const int version = root.value(QStringLiteral("version")).toInt();
    switch (classifyDocumentVersion(version)) {
    case DocVersionStatus::Invalid:
        if (error) {
            *error = tr("Unsupported document version %1.").arg(version);
        }
        return false;
    case DocVersionStatus::TooNew:
        if (error) {
            *error = tr("Document version %1 is newer than this application supports "
                        "(version %2). Please update the application.")
                         .arg(version)
                         .arg(kDocumentVersion);
        }
        return false;
    case DocVersionStatus::Older: {
        const QString note = tr("Document version %1 is older than the current version %2; "
                                "loaded best-effort.")
                                 .arg(version)
                                 .arg(kDocumentVersion);
        m_loadWarning = m_loadWarning.isEmpty() ? note : m_loadWarning + QLatin1Char('\n') + note;
        break;
    }
    case DocVersionStatus::Ok:
        break;
    }

    const QJsonObject settings = root.value(QStringLiteral("settings")).toObject();
    std::vector<TwoPortLoadData> twoPorts;
    std::vector<NodeSnapshot> nodes;
    std::vector<BranchLoadData> branches;

    for (const QJsonValue& value : root.value(QStringLiteral("twoPorts")).toArray()) {
        twoPorts.push_back(twoPortFromJson(value.toObject()));
    }
    for (const QJsonValue& value : root.value(QStringLiteral("nodes")).toArray()) {
        nodes.push_back(nodeFromJson(value.toObject()));
    }
    for (const QJsonValue& value : root.value(QStringLiteral("branches")).toArray()) {
        branches.push_back(branchFromJson(value.toObject()));
    }

    std::vector<SavedNormalTree> savedNormalTrees;
    for (const QJsonValue& value : root.value(QStringLiteral("normalTrees")).toArray()) {
        savedNormalTrees.push_back(savedNormalTreeFromJson(value.toObject()));
    }
    const int activeNormalTreeIndex = root.value(QStringLiteral("activeNormalTreeIndex")).toInt(-1);

    QSet<int> twoPortBranchSerialIds;
    for (const TwoPortLoadData& data : twoPorts) {
        if (data.leftBranch.serialId > 0) {
            twoPortBranchSerialIds.insert(data.leftBranch.serialId);
        }
        if (data.rightBranch.serialId > 0) {
            twoPortBranchSerialIds.insert(data.rightBranch.serialId);
        }
    }

    const int savedNextNodeId = settings.value(QStringLiteral("nextNodeId")).toInt(1);
    const int savedNextBranchId = settings.value(QStringLiteral("nextBranchId")).toInt(1);
    const int savedNextSourceInputId = settings.value(QStringLiteral("nextSourceInputId")).toInt(1);
    const int savedNextTwoPortId = settings.value(QStringLiteral("nextTwoPortId")).toInt(1);

    clearDocument();

    m_defaultSystemType =
        static_cast<SystemType>(settings.value(QStringLiteral("defaultSystemType")).toInt());
    m_snapToGrid = settings.value(QStringLiteral("snapToGrid")).toBool(true);
    m_showGrid = settings.value(QStringLiteral("showGrid")).toBool(true);
    m_gridSpacing = settings.value(QStringLiteral("gridSpacing")).toDouble(20.0);

    m_suppressGraphChange = true;
    for (const TwoPortLoadData& data : twoPorts) {
        NodeItem* v1 = getOrCreateNodeAt(this, data.v1);
        NodeItem* v2 = getOrCreateNodeAt(this, data.v2);
        NodeItem* g1 = getOrCreateNodeAt(this, data.g1);
        NodeItem* g2 = data.sharedRef ? g1 : getOrCreateNodeAt(this, data.g2);
        if (!v1 || !v2 || !g1 || !g2) {
            continue;
        }
        if (TwoPortItem* twoPort =
                createTwoPortFromPorts(v1, v2, g1, g2, data.key.kind, data.key.modulus, data.key.name)) {
            applyLoadedBranchData(this, twoPort->leftBranch(), data.leftBranch);
            applyLoadedBranchData(this, twoPort->rightBranch(), data.rightBranch);
            if (data.leftBranch.serialId >= m_nextBranchId) {
                m_nextBranchId = data.leftBranch.serialId + 1;
            }
            if (data.rightBranch.serialId >= m_nextBranchId) {
                m_nextBranchId = data.rightBranch.serialId + 1;
            }
        }
    }

    for (const NodeSnapshot& snap : nodes) {
        if (findNodeAt(this, snap.pos)) {
            continue;
        }
        if (NodeItem* node = createNode(snap.pos)) {
            node->setName(snap.name);
            node->setGround(snap.ground);
            if (!snap.ground) {
                node->setAcrossVariable(snap.across);
                node->setSystemType(snap.systemType);
            }
        }
    }

    ensureSceneNodesForBranchEndpoints(this, branches);

    std::sort(branches.begin(), branches.end(), [](const BranchLoadData& a, const BranchLoadData& b) {
        if (a.snap.from != b.snap.from) {
            return a.snap.from.x() < b.snap.from.x() ||
                   (a.snap.from.x() == b.snap.from.x() && a.snap.from.y() < b.snap.from.y());
        }
        if (a.snap.to != b.snap.to) {
            return a.snap.to.x() < b.snap.to.x() ||
                   (a.snap.to.x() == b.snap.to.x() && a.snap.to.y() < b.snap.to.y());
        }
        return a.snap.index < b.snap.index;
    });

    for (const BranchLoadData& data : branches) {
        if (data.serialId > 0 && twoPortBranchSerialIds.contains(data.serialId)) {
            continue;
        }
        NodeItem* a = nodeAtPos(data.snap.from);
        NodeItem* b = nodeAtPos(data.snap.to);
        if (!a || !b) {
            continue;
        }
        if (BranchItem* branch = createBranch(a, b, data.snap.bow)) {
            applyLoadedBranchData(this, branch, data);
            if (data.serialId >= m_nextBranchId) {
                m_nextBranchId = data.serialId + 1;
            }
        }
    }
    m_suppressGraphChange = false;

    m_nextNodeId = std::max(m_nextNodeId, savedNextNodeId);
    m_nextBranchId = std::max(m_nextBranchId, savedNextBranchId);
    m_nextSourceInputId = std::max(m_nextSourceInputId, savedNextSourceInputId);
    m_nextTwoPortId = std::max(m_nextTwoPortId, savedNextTwoPortId);

    m_savedNormalTrees = std::move(savedNormalTrees);
    m_activeSavedNormalTreeIndex = -1;
    if (activeNormalTreeIndex >= 0 &&
        activeNormalTreeIndex < static_cast<int>(m_savedNormalTrees.size())) {
        if (!applySavedNormalTree(activeNormalTreeIndex)) {
            m_activeSavedNormalTreeIndex = -1;
        }
    }

    m_undoStack.clear();
    refreshAppearance();
    notifyGraphChanged();
    emit savedNormalTreesChanged();
    return true;
}
