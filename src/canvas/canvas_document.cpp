#include "canvas.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <vector>

namespace {

constexpr int kDocumentVersion = 1;

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

struct BranchLoadData {
    GraphScene::BranchSnapshot snap;
    int serialId = 0;
    int sourceInputId = 0;
};

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

QJsonObject twoPortToJson(const GraphScene::TwoPortKey& key) {
    return QJsonObject{{QStringLiteral("center"), pointToJson(key.center)},
                       {QStringLiteral("kind"), static_cast<int>(key.kind)},
                       {QStringLiteral("modulus"), key.modulus},
                       {QStringLiteral("name"), key.name}};
}

GraphScene::TwoPortKey twoPortFromJson(const QJsonObject& obj) {
    GraphScene::TwoPortKey key;
    key.center = pointFromJson(obj.value(QStringLiteral("center")).toObject());
    key.kind = static_cast<TwoPortKind>(obj.value(QStringLiteral("kind")).toInt());
    key.modulus = obj.value(QStringLiteral("modulus")).toString();
    key.name = obj.value(QStringLiteral("name")).toString();
    return key;
}

}  // namespace

void GraphScene::clearDocument() {
    m_undoStack.clear();
    clearBranchPending();
    clearSelection();
    clearNormalTreeHighlight();

    std::vector<TwoPortItem*> twoPorts;
    std::vector<BranchItem*> branches;
    std::vector<NodeItem*> nodes;
    for (QGraphicsItem* item : items()) {
        if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            twoPorts.push_back(twoPort);
        } else if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branches.push_back(branch);
        } else if (auto* node = dynamic_cast<NodeItem*>(item)) {
            nodes.push_back(node);
        }
    }

    m_suppressGraphChange = true;
    for (TwoPortItem* twoPort : twoPorts) {
        destroyTwoPort(twoPort);
    }
    for (BranchItem* branch : branches) {
        if (branch && branch->scene()) {
            destroyBranch(branch);
        }
    }
    for (NodeItem* node : nodes) {
        if (node && node->scene()) {
            destroyNode(node);
        }
    }
    m_suppressGraphChange = false;

    m_nextNodeId = 1;
    m_nextBranchId = 1;
    m_nextSourceInputId = 1;
    m_nextTwoPortId = 1;
    m_lastNormalTreeResult = {};
    m_lastStateSpaceResult = {};
    notifyGraphChanged();
}

QByteArray GraphScene::documentToJson() const {
    std::vector<TwoPortKey> twoPorts;
    std::vector<NodeSnapshot> nodes;
    struct BranchRecord {
        BranchSnapshot snap;
        int serialId = 0;
        int sourceInputId = 0;
    };
    std::vector<BranchRecord> branches;

    for (QGraphicsItem* item : items()) {
        if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            twoPorts.push_back({snap(twoPort->center()), twoPort->kind(), twoPort->modulus(), twoPort->name()});
        }
    }

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

    QJsonArray twoPortArray;
    for (const TwoPortKey& key : twoPorts) {
        twoPortArray.append(twoPortToJson(key));
    }

    QJsonArray nodeArray;
    for (const NodeSnapshot& snap : nodes) {
        nodeArray.append(nodeToJson(snap));
    }

    QJsonArray branchArray;
    for (const BranchRecord& record : branches) {
        branchArray.append(branchToJson(record.snap, record.serialId, record.sourceInputId));
    }

    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("LinearGraphModeling")},
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
    };

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
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("LinearGraphModeling")) {
        if (error) {
            *error = tr("Not a Linear Graph Modeling document.");
        }
        return false;
    }
    const int version = root.value(QStringLiteral("version")).toInt();
    if (version != kDocumentVersion) {
        if (error) {
            *error = tr("Unsupported document version %1.").arg(version);
        }
        return false;
    }

    const QJsonObject settings = root.value(QStringLiteral("settings")).toObject();
    std::vector<TwoPortKey> twoPorts;
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

    clearDocument();

    m_defaultSystemType =
        static_cast<SystemType>(settings.value(QStringLiteral("defaultSystemType")).toInt());
    m_snapToGrid = settings.value(QStringLiteral("snapToGrid")).toBool(true);
    m_showGrid = settings.value(QStringLiteral("showGrid")).toBool(true);
    m_gridSpacing = settings.value(QStringLiteral("gridSpacing")).toDouble(20.0);
    m_nextNodeId = settings.value(QStringLiteral("nextNodeId")).toInt(1);
    m_nextBranchId = settings.value(QStringLiteral("nextBranchId")).toInt(1);
    m_nextSourceInputId = settings.value(QStringLiteral("nextSourceInputId")).toInt(1);
    m_nextTwoPortId = settings.value(QStringLiteral("nextTwoPortId")).toInt(1);

    m_suppressGraphChange = true;
    for (const TwoPortKey& key : twoPorts) {
        createTwoPort(key.center, key.kind, key.modulus, key.name);
    }

    for (const NodeSnapshot& snap : nodes) {
        if (NodeItem* node = createNode(snap.pos)) {
            node->setName(snap.name);
            node->setGround(snap.ground);
            if (!snap.ground) {
                node->setAcrossVariable(snap.across);
                node->setSystemType(snap.systemType);
            }
        }
    }

    for (const BranchLoadData& data : branches) {
        NodeItem* a = nodeAtPos(data.snap.from);
        NodeItem* b = nodeAtPos(data.snap.to);
        if (!a || !b) {
            continue;
        }
        if (BranchItem* branch = createBranch(a, b, data.snap.bow)) {
            branch->setBranchType(data.snap.type);
            branch->setElementConstant(data.snap.constant);
            if (data.serialId > 0) {
                branch->setSerialId(data.serialId);
                if (data.serialId >= m_nextBranchId) {
                    m_nextBranchId = data.serialId + 1;
                }
            }
            if (data.sourceInputId > 0) {
                branch->setSourceInputId(data.sourceInputId);
                registerSourceInputId(data.sourceInputId);
            }
            branch->setName(data.snap.name);
            branch->setActive(data.snap.active);
        }
    }
    m_suppressGraphChange = false;

    m_undoStack.clear();
    refreshAppearance();
    notifyGraphChanged();
    return true;
}
