#pragma once

#include "canvas.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

// .lgm format version 4: every node and branch is stored once with its id; branches and two-ports
// refer to nodes (and two-ports to their port branches) by id. Positions are layout only.
constexpr int kDocumentVersion = 4;

struct DocumentData {
    SystemType defaultSystemType = SystemType::Mechanical;
    bool snapToGrid = true;
    bool showGrid = true;
    qreal gridSpacing = 20.0;
    int nextNodeId = 1;
    int nextBranchId = 1;
    int nextSourceInputId = 1;
    int nextTwoPortId = 1;

    struct TwoPort {
        GraphScene::TwoPortKey key;
        int v1 = 0;
        int v2 = 0;
        int g1 = 0;
        int g2 = 0;
        int leftBranch = 0;
        int rightBranch = 0;
    };

    std::vector<GraphScene::NodeSnapshot> nodes;
    std::vector<GraphScene::BranchSnapshot> branches;  // port branches included
    std::vector<TwoPort> twoPorts;
    std::vector<GraphScene::SavedNormalTree> normalTrees;
    int activeNormalTreeIndex = -1;
    QStringList outputVariables;
};

QString systemTypeKey(SystemType type);
QString branchTypeKey(BranchType type);
QString twoPortKindKey(TwoPortKind kind);

QJsonObject nodeToJson(const NodeItem* node);
QJsonObject branchToJson(const BranchItem* branch);
QJsonObject twoPortToJson(const TwoPortItem* twoPort);
QJsonObject savedNormalTreeToJson(const GraphScene::SavedNormalTree& tree);

// Parses and validates a whole document (ids unique, every reference resolves, port branches join
// their two-port's nodes) without touching any scene; on failure `error` says what is wrong.
bool parseDocument(const QByteArray& data, DocumentData& out, QString* error);
