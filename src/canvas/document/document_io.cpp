#include "canvas.h"
#include "document_detail.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QSet>

#include <algorithm>
#include <map>

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
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    collectGraphItems(nodes, branches, twoPorts);
    // Sorted by id so saving the same graph always produces the same file.
    const auto byId = [](const auto* a, const auto* b) { return a->serialId() < b->serialId(); };
    std::sort(nodes.begin(), nodes.end(), byId);
    std::sort(branches.begin(), branches.end(), byId);
    std::sort(twoPorts.begin(), twoPorts.end(), byId);

    QJsonArray nodeArray;
    for (const NodeItem* node : nodes) {
        nodeArray.append(nodeToJson(node));
    }
    QJsonArray branchArray;
    for (const BranchItem* branch : branches) {
        branchArray.append(branchToJson(branch));
    }
    QJsonArray twoPortArray;
    for (const TwoPortItem* twoPort : twoPorts) {
        twoPortArray.append(twoPortToJson(twoPort));
    }
    QJsonArray normalTreeArray;
    for (const SavedNormalTree& tree : m_savedNormalTrees) {
        normalTreeArray.append(savedNormalTreeToJson(tree));
    }

    QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("LGM")},
        {QStringLiteral("version"), kDocumentVersion},
        {QStringLiteral("settings"),
         QJsonObject{{QStringLiteral("defaultSystemType"), systemTypeKey(m_defaultSystemType)},
                     {QStringLiteral("snapToGrid"), m_snapToGrid},
                     {QStringLiteral("showGrid"), m_showGrid},
                     {QStringLiteral("gridSpacing"), m_gridSpacing},
                     {QStringLiteral("nextNodeId"), m_nextNodeId},
                     {QStringLiteral("nextBranchId"), m_nextBranchId},
                     {QStringLiteral("nextSourceInputId"), m_nextSourceInputId},
                     {QStringLiteral("nextTwoPortId"), m_nextTwoPortId}}},
        {QStringLiteral("nodes"), nodeArray},
        {QStringLiteral("branches"), branchArray},
        {QStringLiteral("twoPorts"), twoPortArray},
        {QStringLiteral("normalTrees"), normalTreeArray},
    };
    if (!m_outputVariables.isEmpty()) {
        root.insert(QStringLiteral("outputVariables"), QJsonArray::fromStringList(m_outputVariables));
    }
    if (m_activeSavedNormalTreeIndex >= 0) {
        root.insert(QStringLiteral("activeNormalTreeIndex"), m_activeSavedNormalTreeIndex);
    }

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool GraphScene::documentFromJson(const QByteArray& data, QString* error) {
    DocumentData doc;
    if (!parseDocument(data, doc, error)) {
        return false;  // the current document is left untouched
    }

    clearDocument();
    m_defaultSystemType = doc.defaultSystemType;
    m_snapToGrid = doc.snapToGrid;
    m_showGrid = doc.showGrid;
    m_gridSpacing = std::max<qreal>(1.0, doc.gridSpacing);

    // Fresh ids handed out while items are created are overwritten by the stored ones; start the
    // counters above every stored id so the two never collide.
    for (const NodeSnapshot& node : doc.nodes) {
        m_nextNodeId = std::max(m_nextNodeId, node.id + 1);
    }
    for (const BranchSnapshot& branch : doc.branches) {
        m_nextBranchId = std::max(m_nextBranchId, branch.serialId + 1);
    }
    for (const DocumentData::TwoPort& twoPort : doc.twoPorts) {
        m_nextTwoPortId = std::max(m_nextTwoPortId, twoPort.key.id + 1);
    }

    m_suppressGraphChange = true;
    for (const NodeSnapshot& node : doc.nodes) {
        createNodeFromSnapshot(node);
    }

    std::map<int, const BranchSnapshot*> branchById;
    for (const BranchSnapshot& branch : doc.branches) {
        branchById[branch.serialId] = &branch;
    }
    QSet<int> portBranchIds;
    for (const DocumentData::TwoPort& data : doc.twoPorts) {
        TwoPortItem* twoPort = createTwoPortFromPorts(nodeById(data.v1), nodeById(data.v2), nodeById(data.g1),
                                                      nodeById(data.g2), data.key.kind, data.key.modulus,
                                                      data.key.name);
        if (!twoPort) {
            continue;
        }
        twoPort->setSerialId(data.key.id);
        applyBranchSnapshot(twoPort->leftBranch(), *branchById[data.leftBranch]);
        applyBranchSnapshot(twoPort->rightBranch(), *branchById[data.rightBranch]);
        portBranchIds.insert(data.leftBranch);
        portBranchIds.insert(data.rightBranch);
        twoPort->refresh();
    }

    for (const BranchSnapshot& branch : doc.branches) {
        if (!portBranchIds.contains(branch.serialId)) {
            createBranchFromSnapshot(branch);
        }
    }
    m_suppressGraphChange = false;

    m_nextNodeId = std::max(m_nextNodeId, doc.nextNodeId);
    m_nextBranchId = std::max(m_nextBranchId, doc.nextBranchId);
    m_nextSourceInputId = std::max(m_nextSourceInputId, doc.nextSourceInputId);
    m_nextTwoPortId = std::max(m_nextTwoPortId, doc.nextTwoPortId);

    m_savedNormalTrees = std::move(doc.normalTrees);
    m_activeSavedNormalTreeIndex = -1;
    if (doc.activeNormalTreeIndex >= 0 &&
        doc.activeNormalTreeIndex < static_cast<int>(m_savedNormalTrees.size())) {
        if (!applySavedNormalTree(doc.activeNormalTreeIndex)) {
            m_activeSavedNormalTreeIndex = -1;
        }
    }

    m_undoStack.clear();
    m_outputVariables = std::move(doc.outputVariables);
    refreshAppearance();
    notifyGraphChanged();
    emit savedNormalTreesChanged();
    return true;
}
