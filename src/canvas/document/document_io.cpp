#include "canvas.h"
#include "document_detail.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
    m_outputVariables.clear();
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
    if (!m_outputVariables.isEmpty()) {
        QJsonArray outputArray;
        for (const QString& symbol : m_outputVariables) {
            outputArray.append(symbol);
        }
        root.insert(QStringLiteral("outputVariables"), outputArray);
    }
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
    QStringList savedOutputVariables;
    for (const QJsonValue& value : root.value(QStringLiteral("outputVariables")).toArray()) {
        const QString symbol = value.toString().trimmed();
        if (!symbol.isEmpty()) {
            savedOutputVariables.push_back(symbol);
        }
    }

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
    m_outputVariables = std::move(savedOutputVariables);
    refreshAppearance();
    notifyGraphChanged();
    emit savedNormalTreesChanged();
    return true;
}
