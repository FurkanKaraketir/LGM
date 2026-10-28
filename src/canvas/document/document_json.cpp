#include "canvas.h"
#include "document_detail.h"

#include "elemental_equation.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <map>

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("LgmDocument", text);
}

const std::map<SystemType, QString>& systemTypeKeys() {
    static const std::map<SystemType, QString> keys = {
        {SystemType::Mechanical, QStringLiteral("mechanical")},
        {SystemType::MechanicalRotational, QStringLiteral("rotational")},
        {SystemType::Electrical, QStringLiteral("electrical")},
        {SystemType::Fluid, QStringLiteral("fluid")},
        {SystemType::Heat, QStringLiteral("heat")},
    };
    return keys;
}

const std::map<BranchType, QString>& branchTypeKeys() {
    static const std::map<BranchType, QString> keys = {
        {BranchType::A, QStringLiteral("A")},
        {BranchType::T, QStringLiteral("T")},
        {BranchType::D, QStringLiteral("D")},
    };
    return keys;
}

const std::map<TwoPortKind, QString>& twoPortKindKeys() {
    static const std::map<TwoPortKind, QString> keys = {
        {TwoPortKind::Transformer, QStringLiteral("transformer")},
        {TwoPortKind::Gyrator, QStringLiteral("gyrator")},
    };
    return keys;
}

template <typename Enum>
bool enumFromKey(const std::map<Enum, QString>& keys, const QString& key, Enum* out) {
    for (const auto& [value, name] : keys) {
        if (name == key) {
            *out = value;
            return true;
        }
    }
    return false;
}

QJsonObject pointToJson(const QPointF& p) {
    return QJsonObject{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
}

// Collects the first validation error with the offending item in it.
class Parser {
public:
    explicit Parser(QString* error) : m_error(error) {}

    bool ok() const { return m_ok; }

    void fail(const QString& message) {
        if (m_ok && m_error) {
            *m_error = message;
        }
        m_ok = false;
    }

    int id(const QJsonObject& obj, const QString& key, const QString& what) {
        const QJsonValue value = obj.value(key);
        const int result = value.isDouble() ? value.toInt(0) : 0;
        if (result <= 0 || value.toDouble() != result) {
            fail(tr("%1: \"%2\" must be a positive integer id.").arg(what, key));
            return 0;
        }
        return result;
    }

    QString string(const QJsonObject& obj, const QString& key, const QString& what, bool required,
                   const QString& fallback = QString()) {
        const QJsonValue value = obj.value(key);
        if (value.isUndefined()) {
            if (required) {
                fail(tr("%1: missing \"%2\".").arg(what, key));
            }
            return fallback;
        }
        if (!value.isString()) {
            fail(tr("%1: \"%2\" must be a string.").arg(what, key));
            return fallback;
        }
        return value.toString();
    }

    bool boolean(const QJsonObject& obj, const QString& key, const QString& what, bool fallback) {
        const QJsonValue value = obj.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        if (!value.isBool()) {
            fail(tr("%1: \"%2\" must be true or false.").arg(what, key));
        }
        return value.toBool(fallback);
    }

    qreal number(const QJsonObject& obj, const QString& key, const QString& what, bool required,
                 qreal fallback = 0.0) {
        const QJsonValue value = obj.value(key);
        if (value.isUndefined() && !required) {
            return fallback;
        }
        if (!value.isDouble()) {
            fail(tr("%1: \"%2\" must be a number.").arg(what, key));
            return fallback;
        }
        return value.toDouble();
    }

    template <typename Enum>
    Enum enumValue(const QJsonObject& obj, const QString& key, const QString& what,
                   const std::map<Enum, QString>& keys, bool required, Enum fallback) {
        const QJsonValue value = obj.value(key);
        if (value.isUndefined() && !required) {
            return fallback;
        }
        Enum result = fallback;
        if (!value.isString() || !enumFromKey(keys, value.toString(), &result)) {
            QStringList allowed;
            for (const auto& entry : keys) {
                allowed << entry.second;
            }
            fail(tr("%1: \"%2\" must be one of %3.").arg(what, key, allowed.join(QStringLiteral(", "))));
        }
        return result;
    }

    QJsonArray array(const QJsonObject& obj, const QString& key) {
        const QJsonValue value = obj.value(key);
        if (value.isUndefined()) {
            return {};
        }
        if (!value.isArray()) {
            fail(tr("\"%1\" must be an array.").arg(key));
            return {};
        }
        return value.toArray();
    }

    QJsonObject object(const QJsonValue& value, const QString& what) {
        if (!value.isObject()) {
            fail(tr("%1 must be a JSON object.").arg(what));
            return {};
        }
        return value.toObject();
    }

private:
    QString* m_error;
    bool m_ok = true;
};

}  // namespace

QString systemTypeKey(SystemType type) {
    return systemTypeKeys().at(type);
}

QString branchTypeKey(BranchType type) {
    return branchTypeKeys().at(type);
}

QString twoPortKindKey(TwoPortKind kind) {
    return twoPortKindKeys().at(kind);
}

QJsonObject nodeToJson(const NodeItem* node) {
    QJsonObject obj{{QStringLiteral("id"), node->serialId()},
                    {QStringLiteral("name"), node->name()},
                    {QStringLiteral("pos"), pointToJson(node->scenePos())},
                    {QStringLiteral("systemType"), systemTypeKey(node->systemType())}};
    if (node->isGround()) {
        obj.insert(QStringLiteral("ground"), true);
    } else {
        obj.insert(QStringLiteral("across"), node->acrossVariable());
    }
    return obj;
}

QJsonObject branchToJson(const BranchItem* branch) {
    QJsonObject obj{{QStringLiteral("id"), branch->serialId()},
                    {QStringLiteral("from"), branch->from()->serialId()},
                    {QStringLiteral("to"), branch->to()->serialId()},
                    {QStringLiteral("name"), branch->name()},
                    {QStringLiteral("type"), branchTypeKey(branch->branchType())}};
    if (branch->isActive()) {
        obj.insert(QStringLiteral("active"), true);
        obj.insert(QStringLiteral("sourceInputId"), branch->sourceInputId());
    } else if (!branch->isTwoPortPort()) {  // a port's law comes from its two-port's modulus
        obj.insert(QStringLiteral("constant"), branch->elementConstant());
    }
    if (!branch->isTwoPortPort() && branch->bow() != 0.0) {
        obj.insert(QStringLiteral("bow"), branch->bow());
    }
    return obj;
}

QJsonObject twoPortToJson(const TwoPortItem* twoPort) {
    return QJsonObject{{QStringLiteral("id"), twoPort->serialId()},
                       {QStringLiteral("name"), twoPort->name()},
                       {QStringLiteral("kind"), twoPortKindKey(twoPort->kind())},
                       {QStringLiteral("modulus"), twoPort->modulus()},
                       {QStringLiteral("v1"), twoPort->v1()->serialId()},
                       {QStringLiteral("v2"), twoPort->v2()->serialId()},
                       {QStringLiteral("g1"), twoPort->g1()->serialId()},
                       {QStringLiteral("g2"), twoPort->g2()->serialId()},
                       {QStringLiteral("leftBranch"), twoPort->leftBranch()->serialId()},
                       {QStringLiteral("rightBranch"), twoPort->rightBranch()->serialId()}};
}

QJsonObject savedNormalTreeToJson(const GraphScene::SavedNormalTree& tree) {
    QJsonArray branchIds;
    for (int serialId : tree.treeBranchSerialIds) {
        branchIds.append(serialId);
    }
    return QJsonObject{{QStringLiteral("name"), tree.name}, {QStringLiteral("branches"), branchIds}};
}

bool parseDocument(const QByteArray& data, DocumentData& out, QString* error) {
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
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("LGM")) {
        if (error) {
            *error = tr("Not an LGM document.");
        }
        return false;
    }
    const int version = root.value(QStringLiteral("version")).toInt();
    if (version != kDocumentVersion) {
        if (error) {
            *error = version > kDocumentVersion
                         ? tr("Document version %1 is newer than this application supports (version %2). "
                              "Please update the application.")
                               .arg(version)
                               .arg(kDocumentVersion)
                         : tr("Unsupported document version %1; this application reads version %2.")
                               .arg(version)
                               .arg(kDocumentVersion);
        }
        return false;
    }

    Parser p(error);
    out = DocumentData{};

    const QJsonObject settings = root.value(QStringLiteral("settings")).toObject();
    const QString settingsWhat = tr("settings");
    out.defaultSystemType = p.enumValue(settings, QStringLiteral("defaultSystemType"), settingsWhat,
                                        systemTypeKeys(), false, SystemType::Mechanical);
    out.snapToGrid = p.boolean(settings, QStringLiteral("snapToGrid"), settingsWhat, true);
    out.showGrid = p.boolean(settings, QStringLiteral("showGrid"), settingsWhat, true);
    out.gridSpacing = p.number(settings, QStringLiteral("gridSpacing"), settingsWhat, false, 20.0);
    out.nextNodeId = static_cast<int>(p.number(settings, QStringLiteral("nextNodeId"), settingsWhat, false, 1));
    out.nextBranchId =
        static_cast<int>(p.number(settings, QStringLiteral("nextBranchId"), settingsWhat, false, 1));
    out.nextSourceInputId =
        static_cast<int>(p.number(settings, QStringLiteral("nextSourceInputId"), settingsWhat, false, 1));
    out.nextTwoPortId =
        static_cast<int>(p.number(settings, QStringLiteral("nextTwoPortId"), settingsWhat, false, 1));

    QSet<int> nodeIds;
    for (const QJsonValue& value : p.array(root, QStringLiteral("nodes"))) {
        const QJsonObject obj = p.object(value, tr("Node entry"));
        GraphScene::NodeSnapshot node;
        node.id = p.id(obj, QStringLiteral("id"), tr("Node"));
        const QString what = tr("Node %1").arg(node.id);
        if (node.id > 0 && nodeIds.contains(node.id)) {
            p.fail(tr("Duplicate node id %1.").arg(node.id));
        }
        nodeIds.insert(node.id);
        const QJsonObject pos = p.object(obj.value(QStringLiteral("pos")), tr("%1 \"pos\"").arg(what));
        node.pos = {p.number(pos, QStringLiteral("x"), what, true), p.number(pos, QStringLiteral("y"), what, true)};
        node.name = p.string(obj, QStringLiteral("name"), what, false);
        node.ground = p.boolean(obj, QStringLiteral("ground"), what, false);
        node.systemType =
            p.enumValue(obj, QStringLiteral("systemType"), what, systemTypeKeys(), true, SystemType::Mechanical);
        node.across = node.ground ? QStringLiteral("0") : p.string(obj, QStringLiteral("across"), what, true);
        if (!node.ground && node.across.trimmed().isEmpty()) {
            p.fail(tr("%1: \"across\" must not be empty.").arg(what));
        }
        out.nodes.push_back(node);
    }

    std::map<int, size_t> branchIndex;
    for (const QJsonValue& value : p.array(root, QStringLiteral("branches"))) {
        const QJsonObject obj = p.object(value, tr("Branch entry"));
        GraphScene::BranchSnapshot branch;
        branch.serialId = p.id(obj, QStringLiteral("id"), tr("Branch"));
        const QString what = tr("Branch %1").arg(branch.serialId);
        if (branch.serialId > 0 && branchIndex.count(branch.serialId)) {
            p.fail(tr("Duplicate branch id %1.").arg(branch.serialId));
        }
        branch.fromId = p.id(obj, QStringLiteral("from"), what);
        branch.toId = p.id(obj, QStringLiteral("to"), what);
        if (p.ok() && (!nodeIds.contains(branch.fromId) || !nodeIds.contains(branch.toId))) {
            p.fail(tr("%1 refers to a node id that does not exist.").arg(what));
        }
        if (p.ok() && branch.fromId == branch.toId) {
            p.fail(tr("%1 starts and ends on the same node.").arg(what));
        }
        branch.name = p.string(obj, QStringLiteral("name"), what, false);
        branch.type = p.enumValue(obj, QStringLiteral("type"), what, branchTypeKeys(), true, BranchType::A);
        branch.active = p.boolean(obj, QStringLiteral("active"), what, false);
        if (branch.active && branch.type == BranchType::D) {
            p.fail(tr("%1: a source must be A- or T-type.").arg(what));
        }
        branch.constant =
            branch.active ? QString() : p.string(obj, QStringLiteral("constant"), what, false, QStringLiteral("1"));
        branch.sourceInputId =
            branch.active ? static_cast<int>(p.number(obj, QStringLiteral("sourceInputId"), what, false, 0)) : 0;
        branch.bow = p.number(obj, QStringLiteral("bow"), what, false, 0.0);
        branchIndex[branch.serialId] = out.branches.size();
        out.branches.push_back(branch);
    }

    QSet<int> twoPortIds;
    QSet<int> portBranchIds;
    for (const QJsonValue& value : p.array(root, QStringLiteral("twoPorts"))) {
        const QJsonObject obj = p.object(value, tr("Two-port entry"));
        DocumentData::TwoPort twoPort;
        twoPort.key.id = p.id(obj, QStringLiteral("id"), tr("Two-port"));
        const QString what = tr("Two-port %1").arg(twoPort.key.id);
        if (twoPort.key.id > 0 && twoPortIds.contains(twoPort.key.id)) {
            p.fail(tr("Duplicate two-port id %1.").arg(twoPort.key.id));
        }
        twoPortIds.insert(twoPort.key.id);
        twoPort.key.name = p.string(obj, QStringLiteral("name"), what, false);
        twoPort.key.kind =
            p.enumValue(obj, QStringLiteral("kind"), what, twoPortKindKeys(), true, TwoPortKind::Transformer);
        twoPort.key.modulus = p.string(obj, QStringLiteral("modulus"), what, false);
        twoPort.v1 = p.id(obj, QStringLiteral("v1"), what);
        twoPort.v2 = p.id(obj, QStringLiteral("v2"), what);
        twoPort.g1 = p.id(obj, QStringLiteral("g1"), what);
        twoPort.g2 = p.id(obj, QStringLiteral("g2"), what);
        twoPort.leftBranch = p.id(obj, QStringLiteral("leftBranch"), what);
        twoPort.rightBranch = p.id(obj, QStringLiteral("rightBranch"), what);
        if (!p.ok()) {
            break;
        }
        for (int nodeId : {twoPort.v1, twoPort.v2, twoPort.g1, twoPort.g2}) {
            if (!nodeIds.contains(nodeId)) {
                p.fail(tr("%1 refers to node id %2, which does not exist.").arg(what).arg(nodeId));
            }
        }
        if (p.ok() && (twoPort.v1 == twoPort.v2 || twoPort.v1 == twoPort.g1 || twoPort.v1 == twoPort.g2 ||
                       twoPort.v2 == twoPort.g1 || twoPort.v2 == twoPort.g2)) {
            p.fail(tr("%1: across nodes must be distinct from each other and from the references "
                      "(only g1 and g2 may be the same node).")
                       .arg(what));
        }
        const auto checkPort = [&](int branchId, int vId, int gId, const char* side) {
            if (!p.ok()) {
                return;
            }
            if (!branchIndex.count(branchId)) {
                p.fail(tr("%1: %2 port branch %3 does not exist.").arg(what, tr(side)).arg(branchId));
                return;
            }
            if (portBranchIds.contains(branchId)) {
                p.fail(tr("%1: branch %2 is already a port of another two-port.").arg(what).arg(branchId));
                return;
            }
            portBranchIds.insert(branchId);
            const GraphScene::BranchSnapshot& branch = out.branches[branchIndex[branchId]];
            const bool joins = (branch.fromId == vId && branch.toId == gId) ||
                               (branch.fromId == gId && branch.toId == vId);
            if (!joins) {
                p.fail(tr("%1: %2 port branch %3 must join its across and reference nodes.")
                           .arg(what, tr(side))
                           .arg(branchId));
            }
        };
        checkPort(twoPort.leftBranch, twoPort.v1, twoPort.g1, "left");
        checkPort(twoPort.rightBranch, twoPort.v2, twoPort.g2, "right");
        if (!p.ok()) {
            break;
        }
        out.twoPorts.push_back(twoPort);
    }

    // Port branches carry no constant of their own; every other passive branch needs a valid one.
    for (const GraphScene::BranchSnapshot& branch : out.branches) {
        if (p.ok() && !branch.active && !portBranchIds.contains(branch.serialId) &&
            !lg::isValidElementConstant(branch.constant)) {
            p.fail(tr("Branch %1: invalid element constant \"%2\".").arg(branch.serialId).arg(branch.constant));
        }
    }

    for (const QJsonValue& value : p.array(root, QStringLiteral("normalTrees"))) {
        const QJsonObject obj = p.object(value, tr("Saved normal tree"));
        GraphScene::SavedNormalTree tree;
        tree.name = p.string(obj, QStringLiteral("name"), tr("Saved normal tree"), false);
        for (const QJsonValue& id : p.array(obj, QStringLiteral("branches"))) {
            if (!branchIndex.count(id.toInt())) {
                p.fail(tr("Saved normal tree \"%1\" refers to branch id %2, which does not exist.")
                           .arg(tree.name)
                           .arg(id.toInt()));
            }
            tree.treeBranchSerialIds.push_back(id.toInt());
        }
        out.normalTrees.push_back(tree);
    }
    out.activeNormalTreeIndex = root.value(QStringLiteral("activeNormalTreeIndex")).toInt(-1);
    for (const QJsonValue& value : p.array(root, QStringLiteral("outputVariables"))) {
        const QString symbol = value.toString().trimmed();
        if (!symbol.isEmpty()) {
            out.outputVariables.push_back(symbol);
        }
    }
    return p.ok();
}
