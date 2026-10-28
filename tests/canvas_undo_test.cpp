// Canvas regression tests: node/branch/two-port topology, merging, delete and undo/redo.
// Build with -DLGM_BUILD_TESTS=ON and run headless: QT_QPA_PLATFORM=offscreen ctest
#include "canvas.h"

#include <QAbstractButton>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <set>
#include <type_traits>

namespace {

GraphScene* g_scene = nullptr;
int g_failures = 0;

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            ++g_failures;                                                                    \
        }                                                                                    \
    } while (0)

template <typename T>
std::vector<T*> itemsOf() {
    std::vector<T*> out;
    for (QGraphicsItem* item : g_scene->items()) {
        if (auto* typed = dynamic_cast<T*>(item)) {
            out.push_back(typed);
        }
    }
    std::sort(out.begin(), out.end(), [](T* a, T* b) { return a->serialId() < b->serialId(); });
    return out;
}

bool inScene(const QGraphicsItem* item) {
    return item && item->scene() == g_scene;
}

// Every pointer between nodes, branches and two-ports must agree in both directions.
void checkInvariants(const char* where) {
    const int before = g_failures;
    std::set<int> nodeIds;
    for (NodeItem* node : itemsOf<NodeItem>()) {
        CHECK(node->serialId() > 0);
        CHECK(nodeIds.insert(node->serialId()).second);
        for (BranchItem* branch : node->branches()) {
            CHECK(inScene(branch));
            CHECK(branch->from() == node || branch->to() == node);
        }
        for (TwoPortItem* twoPort : node->twoPorts()) {
            CHECK(inScene(twoPort));
            CHECK(twoPort->portRoles(node) != 0);
        }
    }
    std::set<int> branchIds;
    for (BranchItem* branch : itemsOf<BranchItem>()) {
        CHECK(branchIds.insert(branch->serialId()).second);
        CHECK(inScene(branch->from()) && inScene(branch->to()));
        CHECK(branch->from() != branch->to());
        const auto& fromList = branch->from()->branches();
        const auto& toList = branch->to()->branches();
        CHECK(std::count(fromList.begin(), fromList.end(), branch) == 1);
        CHECK(std::count(toList.begin(), toList.end(), branch) == 1);
    }
    std::set<int> twoPortIds;
    for (TwoPortItem* twoPort : itemsOf<TwoPortItem>()) {
        CHECK(twoPortIds.insert(twoPort->serialId()).second);
        for (NodeItem* port : {twoPort->v1(), twoPort->v2(), twoPort->g1(), twoPort->g2()}) {
            CHECK(inScene(port));
            if (inScene(port)) {
                const auto& owners = port->twoPorts();
                CHECK(std::find(owners.begin(), owners.end(), twoPort) != owners.end());
            }
        }
        BranchItem* left = twoPort->leftBranch();
        BranchItem* right = twoPort->rightBranch();
        CHECK(inScene(left) && inScene(right));
        if (inScene(left) && inScene(right)) {
            CHECK(left->isTwoPortPort() && right->isTwoPortPort());
            CHECK((left->from() == twoPort->v1() && left->to() == twoPort->g1()) ||
                  (left->to() == twoPort->v1() && left->from() == twoPort->g1()));
            CHECK((right->from() == twoPort->v2() && right->to() == twoPort->g2()) ||
                  (right->to() == twoPort->v2() && right->from() == twoPort->g2()));
            CHECK(!twoPort->boundingRect().isEmpty());
        }
    }
    if (g_failures != before) {
        std::fprintf(stderr, "  ^ invariant failure at: %s\n", where);
    }
}

// Document content without counters and lane indices (lanes are re-derived on re-creation).
QString fingerprint() {
    QJsonObject root = QJsonDocument::fromJson(g_scene->documentToJson()).object();
    root.remove(QStringLiteral("settings"));
    for (const QString& key : {QStringLiteral("nodes"), QStringLiteral("branches"), QStringLiteral("twoPorts")}) {
        QStringList entries;
        for (const QJsonValue& value : root.value(key).toArray()) {
            QJsonObject obj = value.toObject();
            obj.remove(QStringLiteral("index"));
            for (const QString& sub : {QStringLiteral("leftBranch"), QStringLiteral("rightBranch")}) {
                if (obj.contains(sub)) {
                    QJsonObject inner = obj.value(sub).toObject();
                    inner.remove(QStringLiteral("index"));
                    obj.insert(sub, inner);
                }
            }
            entries << QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        }
        entries.sort();
        root.insert(key, QJsonArray::fromStringList(entries));
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// Prints the document entries present in only one of two fingerprints.
void reportDiff(const QString& expected, const QString& actual) {
    const QJsonObject a = QJsonDocument::fromJson(expected.toUtf8()).object();
    const QJsonObject b = QJsonDocument::fromJson(actual.toUtf8()).object();
    for (const QString& key : a.keys()) {
        if (a.value(key) == b.value(key)) {
            continue;
        }
        if (!a.value(key).isArray()) {
            std::fprintf(stderr, "    %s differs\n", qPrintable(key));
            continue;
        }
        const QJsonArray left = a.value(key).toArray();
        const QJsonArray right = b.value(key).toArray();
        for (const QJsonValue& v : left) {
            if (!right.contains(v)) {
                std::fprintf(stderr, "    - %s %s\n", qPrintable(key), qPrintable(v.toString()));
            }
        }
        for (const QJsonValue& v : right) {
            if (!left.contains(v)) {
                std::fprintf(stderr, "    + %s %s\n", qPrintable(key), qPrintable(v.toString()));
            }
        }
    }
}

void checkSame(const char* what, const QString& expected, const QString& actual) {
    if (expected != actual) {
        std::fprintf(stderr, "  FAIL %s\n", what);
        reportDiff(expected, actual);
        ++g_failures;
    }
}

int countOf(int kind) {
    switch (kind) {
    case 0:
        return static_cast<int>(itemsOf<NodeItem>().size());
    case 1:
        return static_cast<int>(itemsOf<BranchItem>().size());
    default:
        return static_cast<int>(itemsOf<TwoPortItem>().size());
    }
}

void loadExample(const QString& file) {
    QFile f(QStringLiteral(EXAMPLES_SOURCE_DIR "/") + file);
    CHECK(f.open(QIODevice::ReadOnly));
    QString error;
    CHECK(g_scene->documentFromJson(f.readAll(), &error));
}

// Pushes `op`, then checks undo restores the previous document and redo the result.
void roundTrip(const char* name, const std::function<void()>& op) {
    const QString before = fingerprint();
    const int index = g_scene->undoStack()->index();
    op();
    checkInvariants(name);
    const QString after = fingerprint();
    const int pushed = g_scene->undoStack()->index() - index;
    for (int i = 0; i < pushed; ++i) {
        g_scene->undo();
        checkInvariants(name);
    }
    checkSame((QByteArray(name) + ": undo did not restore the document").constData(), before, fingerprint());
    for (int i = 0; i < pushed; ++i) {
        g_scene->redo();
        checkInvariants(name);
    }
    checkSame((QByteArray(name) + ": redo did not reproduce the edit").constData(), after, fingerprint());
}

void testCascadeReferenceChain() {
    g_scene->clearDocument();
    TwoPortItem* t1 = g_scene->createTwoPort({0, 0}, TwoPortKind::Transformer);
    TwoPortItem* t2 = g_scene->createTwoPort({200, 0}, TwoPortKind::Transformer);
    TwoPortItem* t3 = g_scene->createTwoPort({400, 0}, TwoPortKind::Transformer);
    g_scene->undoStack()->clear();
    roundTrip("cascade t2.g1 <- t1.g2", [&] { g_scene->pushMergeNodes(t2->g1(), t1->g2()); });
    // Drag the newcomer onto the shared junction: every owner must be re-pointed.
    roundTrip("cascade t3.g1 <- shared", [&] { g_scene->pushMergeNodes(t3->g1(), t2->g1()); });
    CHECK(t1->g2() == t2->g1() && t2->g1() == t3->g1());
    CHECK(t1->g2()->twoPorts().size() == 3);
    roundTrip("cascade across", [&] { g_scene->pushMergeNodes(t1->v2(), t2->v1()); });
    t1->refresh();
    t2->refresh();
    t3->refresh();
    checkInvariants("cascade refresh");
}

void testMoveRefreshesEveryOwner() {
    g_scene->clearDocument();
    TwoPortItem* t1 = g_scene->createTwoPort({0, 0}, TwoPortKind::Transformer);
    TwoPortItem* t2 = g_scene->createTwoPort({200, 0}, TwoPortKind::Transformer);
    g_scene->pushMergeNodes(t1->v2(), t2->v1());
    NodeItem* shared = t1->v2();
    shared->setPos(shared->pos() + QPointF(0, -100));
    const QPointF centroid =
        (t2->v1()->pos() + t2->v2()->pos() + t2->g1()->pos() + t2->g2()->pos()) / 4.0;
    CHECK(t2->center() == g_scene->snap(centroid));
}

void testPurgeKeepsSharedOwnership() {
    loadExample(QStringLiteral("TransformerCascade.lgm"));
    std::vector<TwoPortItem*> tps = itemsOf<TwoPortItem>();
    CHECK(tps.size() == 3);
    TwoPortItem* t2 = nullptr;
    NodeItem* shared = nullptr;
    roundTrip("delete first cascade stage", [&] {
        g_scene->selectTwoPort(tps[0]);
        g_scene->pushDeleteSelection();
    });
    // roundTrip left the delete applied; the remaining stages were re-created by id.
    t2 = itemsOf<TwoPortItem>()[0];
    shared = t2->v1();
    CHECK(shared->twoPort() == t2);
    roundTrip("delete former shared node", [&] {
        g_scene->clearSelection();
        shared->setSelected(true);
        g_scene->pushDeleteSelection();
    });
}

void testDeleteTwoPortKeepsExternalBranches() {
    g_scene->clearDocument();
    TwoPortItem* tp = g_scene->createTwoPort({0, 0}, TwoPortKind::Transformer);
    NodeItem* n = g_scene->createNode({-200, -60});
    g_scene->createBranch(n, tp->v1())->setElementConstant(QStringLiteral("R1"));
    g_scene->createBranch(tp->v2(), tp->g2())->setElementConstant(QStringLiteral("J2"));
    g_scene->undoStack()->clear();
    roundTrip("delete two-port with externals", [&] {
        g_scene->selectTwoPort(tp);
        g_scene->pushDeleteSelection();
    });
    CHECK(countOf(1) == 0);
    g_scene->undo();
    CHECK(countOf(1) == 4);  // both port branches and both external branches
}

void testMergeUndoRedo() {
    g_scene->clearDocument();
    g_scene->setDefaultSystemType(SystemType::Electrical);
    NodeItem* a = g_scene->createNode({0, 0});
    NodeItem* b = g_scene->createNode({200, 0});
    NodeItem* c = g_scene->createNode({100, 200});
    g_scene->createBranch(a, b)->setElementConstant(QStringLiteral("R7"));
    g_scene->createBranch(b, c);
    g_scene->setDefaultSystemType(SystemType::Fluid);
    g_scene->undoStack()->clear();
    roundTrip("merge plain nodes with a link", [&] { g_scene->pushMergeNodes(a, b); });
    g_scene->undo();
    g_scene->redo();
    g_scene->undo();
    CHECK(countOf(1) == 2);  // the a-b link comes back
    for (NodeItem* node : itemsOf<NodeItem>()) {
        CHECK(node->systemType() == SystemType::Electrical);
    }
}

void testDragMergeRestoresDragStart() {
    g_scene->clearDocument();
    TwoPortItem* tp = g_scene->createTwoPort({0, 0}, TwoPortKind::Transformer);
    NodeItem* n = g_scene->createNode({-200, -60});
    g_scene->createBranch(g_scene->createNode({-300, -60}), n);
    g_scene->undoStack()->clear();
    const QPointF start = n->pos();
    const int nodeId = n->serialId();
    const QString before = fingerprint();
    n->setPos(tp->v1()->pos());
    CHECK(g_scene->tryMergeOverlappingNodes(n, start));
    const QString merged = fingerprint();
    g_scene->undo();
    CHECK(fingerprint() == before);
    CHECK(g_scene->nodeById(nodeId) && g_scene->nodeById(nodeId)->pos() == start);
    g_scene->redo();
    CHECK(fingerprint() == merged);
    checkInvariants("drag merge");
}

void testThreeTerminalCollapse() {
    g_scene->clearDocument();
    TwoPortItem* tp = g_scene->createTwoPort({0, 0}, TwoPortKind::Gyrator);
    g_scene->createBranch(g_scene->createNode({-200, 60}), tp->g2());
    g_scene->undoStack()->clear();
    roundTrip("collapse references", [&] { g_scene->pushMergeNodes(tp->g2(), tp->g1()); });
    CHECK(tp->hasSharedReference());
    QString reason;
    CHECK(!g_scene->canMergeNodes(tp->v1(), tp->g1(), &reason));
}

void testCommandsSurviveRecreation() {
    g_scene->clearDocument();
    NodeItem* a = g_scene->createNode({0, 0});
    NodeItem* b = g_scene->createNode({200, 0});
    g_scene->pushConnectNodes(a, b);
    BranchItem* branch = g_scene->branchesBetween(a, b).front();
    const int id = branch->serialId();
    g_scene->pushFlipBranch(branch);
    g_scene->pushSetBranchName(branch, QStringLiteral("renamed"));
    g_scene->clearSelection();
    branch->setSelected(true);
    g_scene->pushDeleteSelection();
    g_scene->undo();  // re-created as a new object with the same id
    g_scene->undo();  // rename
    g_scene->undo();  // flip
    BranchItem* again = g_scene->branchBySerialId(id);
    CHECK(again && again->from() == a && again->to() == b && again->name() != QStringLiteral("renamed"));
    g_scene->redo();
    g_scene->redo();
    again = g_scene->branchBySerialId(id);
    CHECK(again && again->from() == b && again->name() == QStringLiteral("renamed"));

    TwoPortItem* tp = nullptr;
    g_scene->pushAddTwoPort({400, 0});
    tp = itemsOf<TwoPortItem>().back();
    const int tpId = tp->serialId();
    g_scene->pushSetTwoPortName(tp, QStringLiteral("Motor"));
    g_scene->undo();
    g_scene->undo();
    CHECK(!g_scene->twoPortById(tpId));
    g_scene->redo();
    g_scene->redo();
    CHECK(g_scene->twoPortById(tpId) && g_scene->twoPortById(tpId)->name() == QStringLiteral("Motor"));
    checkInvariants("recreation");
}

void testGroundDomainPersists() {
    g_scene->clearDocument();
    g_scene->setDefaultSystemType(SystemType::Electrical);
    g_scene->createNode({0, 0})->setGround(true);
    g_scene->setDefaultSystemType(SystemType::Mechanical);
    CHECK(g_scene->documentFromJson(g_scene->documentToJson()));
    CHECK(itemsOf<NodeItem>().front()->systemType() == SystemType::Electrical);
}

void testExamplesRoundTrip() {
    for (const QString& file :
         {QStringLiteral("Electrical.lgm"), QStringLiteral("FloatingSource.lgm"), QStringLiteral("Gyrator.lgm"),
          QStringLiteral("Motor.lgm"), QStringLiteral("MultiNormalTree.lgm"),
          QStringLiteral("PumpAccumulator.lgm"), QStringLiteral("TransformerCascade.lgm")}) {
        loadExample(file);
        checkInvariants("load");
        const QString loaded = fingerprint();
        CHECK(g_scene->documentFromJson(g_scene->documentToJson()));
        checkSame("save/load round trip", loaded, fingerprint());
        for (TwoPortItem* tp : itemsOf<TwoPortItem>()) {
            const int id = tp->serialId();
            roundTrip("delete example two-port", [&] {
                g_scene->selectTwoPort(g_scene->twoPortById(id));
                g_scene->pushDeleteSelection();
            });
            g_scene->undo();
        }
        for (NodeItem* node : itemsOf<NodeItem>()) {
            if (node->twoPort()) {
                continue;
            }
            const int id = node->serialId();
            roundTrip("delete example node", [&] {
                g_scene->clearSelection();
                g_scene->nodeById(id)->setSelected(true);
                g_scene->pushDeleteSelection();
            });
            g_scene->undo();
        }
    }
}

// Random edits interleaved with undo/redo; the whole history must unwind and replay exactly.
void fuzz(quint32 seed, int steps) {
    g_scene->clearDocument();
    QRandomGenerator rng(seed);
    const auto pick = [&](const auto& items) {
        using Item = std::remove_reference_t<decltype(items.front())>;
        return items.empty() ? Item(nullptr) : items[rng.bounded(static_cast<int>(items.size()))];
    };
    const auto grid = [&] { return QPointF(rng.bounded(-10, 10) * 40.0, rng.bounded(-8, 8) * 40.0); };
    const QString empty = fingerprint();
    const int baseline = g_failures;
    for (int step = 0; step < steps; ++step) {
        switch (rng.bounded(11)) {
        case 0:
        case 1:
            g_scene->pushAddNode(grid());
            break;
        case 2:
            g_scene->pushAddTwoPort(grid(), rng.bounded(2) ? TwoPortKind::Gyrator : TwoPortKind::Transformer);
            break;
        case 3:
        case 4: {
            NodeItem* a = pick(itemsOf<NodeItem>());
            NodeItem* b = pick(itemsOf<NodeItem>());
            if (a && b && a != b) {
                g_scene->pushConnectNodes(a, b);
            }
            break;
        }
        case 5: {
            NodeItem* a = pick(itemsOf<NodeItem>());
            NodeItem* b = pick(itemsOf<NodeItem>());
            if (a && b && g_scene->canMergeNodes(a, b)) {
                g_scene->pushMergeNodes(a, b);
            }
            break;
        }
        case 6: {
            g_scene->clearSelection();
            const int which = rng.bounded(3);
            QGraphicsItem* item = which == 0   ? static_cast<QGraphicsItem*>(pick(itemsOf<NodeItem>()))
                                  : which == 1 ? static_cast<QGraphicsItem*>(pick(itemsOf<BranchItem>()))
                                               : static_cast<QGraphicsItem*>(pick(itemsOf<TwoPortItem>()));
            if (item) {
                item->setSelected(true);
                g_scene->pushDeleteSelection();
            }
            break;
        }
        case 7:
            if (BranchItem* branch = pick(itemsOf<BranchItem>())) {
                g_scene->pushFlipBranch(branch);
            }
            break;
        case 8:
            if (NodeItem* node = pick(itemsOf<NodeItem>())) {
                const QPointF from = node->pos();
                node->setPos(grid());
                g_scene->pushMove(node, from, node->pos());
            }
            break;
        case 9:
            g_scene->undo();
            break;
        default:
            g_scene->redo();
            break;
        }
        checkInvariants("fuzz step");
        if (g_failures != baseline) {
            std::fprintf(stderr, "  fuzz seed %u failed at step %d\n", seed, step);
            return;
        }
    }
    QUndoStack* stack = g_scene->undoStack();
    while (stack->canRedo()) {
        stack->redo();
    }
    const QString finalState = fingerprint();
    while (stack->canUndo()) {
        stack->undo();
        checkInvariants("fuzz unwind");
    }
    checkSame("fuzz undo-all", empty, fingerprint());
    while (stack->canRedo()) {
        stack->redo();
        checkInvariants("fuzz replay");
    }
    checkSame("fuzz redo-all", finalState, fingerprint());
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    GraphScene scene;
    g_scene = &scene;
    GraphView view(&scene);  // pushMergeNodes asks for confirmation through a view

    QTimer confirm;
    QObject::connect(&confirm, &QTimer::timeout, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            if (QAbstractButton* yes = box->button(QMessageBox::Yes)) {
                yes->click();
            } else {
                box->accept();
            }
        }
    });
    confirm.start(1);

    const std::vector<std::pair<const char*, void (*)()>> tests = {
        {"cascade reference chain", testCascadeReferenceChain},
        {"move refreshes every owner", testMoveRefreshesEveryOwner},
        {"purge keeps shared ownership", testPurgeKeepsSharedOwnership},
        {"delete two-port keeps external branches", testDeleteTwoPortKeepsExternalBranches},
        {"merge undo/redo", testMergeUndoRedo},
        {"drag merge restores drag start", testDragMergeRestoresDragStart},
        {"three-terminal collapse", testThreeTerminalCollapse},
        {"commands survive re-creation", testCommandsSurviveRecreation},
        {"ground domain persists", testGroundDomainPersists},
        {"examples round trip", testExamplesRoundTrip},
    };
    for (const auto& [name, test] : tests) {
        const int before = g_failures;
        test();
        std::printf("%s %s\n", g_failures == before ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    }
    // Optional: canvas_undo_test <seeds> <steps> for a longer fuzz run.
    const quint32 seeds = argc > 1 ? QByteArray(argv[1]).toUInt() : 40;
    const int steps = argc > 2 ? QByteArray(argv[2]).toInt() : 120;
    for (quint32 seed = 1; seed <= seeds; ++seed) {
        const int before = g_failures;
        fuzz(seed, steps);
        if (g_failures != before) {
            std::printf("FAIL fuzz seed %u\n", seed);
            break;
        }
    }
    std::fflush(stdout);
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
