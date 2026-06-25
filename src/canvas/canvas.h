#pragma once



#include <QGraphicsEllipseItem>

#include <QGraphicsItem>

#include <QGraphicsPathItem>

#include <QGraphicsScene>

#include <QGraphicsView>

#include <QString>

#include <QUndoStack>

#include <vector>

#include "normal_tree.h"
#include "state_space.h"



class QGraphicsSceneMouseEvent;

class GraphScene;

class NodeItem;
class TwoPortItem;



enum class TwoPortKind { Transformer, Gyrator };

enum class BranchType { A, T, D };

enum class SystemType { Mechanical, Electrical, Fluid, Heat, MechanicalRotational };



class BranchItem : public QGraphicsPathItem {

public:

    BranchItem(NodeItem* from, NodeItem* to, int index, int count, qreal bow = 0.0);



    void updatePath();

    NodeItem* from() const { return m_from; }

    NodeItem* to() const { return m_to; }

    int index() const { return m_index; }

    void setSlot(int index, int count);

    void setBow(qreal bow);

    bool isActive() const { return m_active; }

    void setActive(bool active);

    BranchType branchType() const { return m_type; }

    void setBranchType(BranchType type);

    QString name() const { return m_name; }

    void setName(const QString& name) { m_name = name; }

    QString elementConstant() const { return m_elementConstant; }

    void setElementConstant(const QString& constant);

    QString elementalEquationText() const;

    void flip();

    qreal bow() const { return m_bow; }

    void replaceEndpoint(NodeItem* oldNode, NodeItem* newNode);

    bool inNormalTree() const { return m_inNormalTree; }

    bool normalTreeRoleKnown() const { return m_normalTreeRoleKnown; }

    void setNormalTreeRole(bool inTree, bool known);

    void refreshTheme();

    int serialId() const { return m_serialId; }

    void setSerialId(int id) { m_serialId = id; }

    int sourceInputId() const { return m_sourceInputId; }

    void setSourceInputId(int id) { m_sourceInputId = id; }

    bool isTwoPortPort() const { return m_twoPortPort; }

    void setTwoPortPort(bool port) { m_twoPortPort = port; }

protected:

    QRectF boundingRect() const override;

    QPainterPath shape() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;



private:

    NodeItem* m_from;

    NodeItem* m_to;

    int m_index;

    int m_count;

    qreal m_bow;

    bool m_active = false;

    BranchType m_type = BranchType::A;

    QString m_name;

    QString m_elementConstant = QStringLiteral("1");

    bool m_inNormalTree = false;

    bool m_normalTreeRoleKnown = false;

    int m_serialId = 0;

    int m_sourceInputId = 0;

    bool m_twoPortPort = false;

    void updateBranchPen();

};



class NodeItem : public QGraphicsEllipseItem {

public:

    explicit NodeItem(qreal radius = 8.0);



    void addBranch(BranchItem* branch);

    void removeBranch(BranchItem* branch);

    const std::vector<BranchItem*>& branches() const { return m_branches; }



    bool isGround() const { return m_ground; }

    void setGround(bool ground);

    QRectF boundingRect() const override;

    TwoPortItem* twoPort() const { return m_twoPort; }

    void setTwoPort(TwoPortItem* twoPort);

    QString name() const { return m_name; }

    void setName(const QString& name) { m_name = name; }

    QString acrossVariable() const { return m_ground ? QStringLiteral("0") : m_acrossVariable; }

    void setAcrossVariable(const QString& symbol);

    SystemType systemType() const { return m_systemType; }

    void setSystemType(SystemType type);

    void refreshTheme();

protected:

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;



private:

    std::vector<BranchItem*> m_branches;

    QPointF m_dragStart;

    bool m_ground = false;

    TwoPortItem* m_twoPort = nullptr;

    QString m_name;

    QString m_acrossVariable;

    SystemType m_systemType = SystemType::Mechanical;

};



class TwoPortItem : public QGraphicsItem {

public:

    TwoPortItem(TwoPortKind kind, const QPointF& center, NodeItem* v1, NodeItem* v2, NodeItem* g1,
                NodeItem* g2, BranchItem* left, BranchItem* right);



    TwoPortKind kind() const { return m_kind; }

    void setKind(TwoPortKind kind);

    QPointF center() const { return m_center; }

    NodeItem* v1() const { return m_v1; }

    NodeItem* v2() const { return m_v2; }

    NodeItem* g1() const { return m_g1; }

    NodeItem* g2() const { return m_g2; }

    BranchItem* leftBranch() const { return m_left; }

    BranchItem* rightBranch() const { return m_right; }

    QString name() const { return m_name; }

    void setName(const QString& name) { m_name = name; }

    QString modulus() const { return m_modulus; }

    void setModulus(const QString& modulus);

    QString elementalEquationText() const;

    void applyPortDefaults();

    void applyInternalBranchBows();



    void refresh();

    void moveBy(const QPointF& delta);

    bool isSyncing() const { return m_syncing; }

    bool hasSharedReference() const { return m_g1 == m_g2; }

    bool collapseSharedRef(NodeItem* gKeep, NodeItem* gRemove);

    void setG1(NodeItem* node) { m_g1 = node; }

    void setG2(NodeItem* node) { m_g2 = node; }

    void setV1(NodeItem* node) { m_v1 = node; }

    void setV2(NodeItem* node) { m_v2 = node; }

    void selectMembers(bool selected);



    QRectF boundingRect() const override;

    QPainterPath shape() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;



private:

    QPointF couplerLeft() const;

    QPointF couplerRight() const;

    QPointF m_dragScenePos;

    QPointF m_dragCenterStart;

    bool m_dragMoved = false;



    TwoPortKind m_kind;

    QPointF m_center;

    NodeItem* m_v1;

    NodeItem* m_v2;

    NodeItem* m_g1;

    NodeItem* m_g2;

    BranchItem* m_left;
    BranchItem* m_right;
    bool m_syncing = false;
    QString m_name;
    QString m_modulus;
};



class GraphScene : public QGraphicsScene {

    Q_OBJECT



public:

    enum class Mode { Select, AddNode, AddBranch, AddTwoPort, SelectNormalTree };



    explicit GraphScene(QObject* parent = nullptr);



    void pushAddNode(const QPointF& center);

    void pushAddTwoPort(const QPointF& center, TwoPortKind kind = TwoPortKind::Transformer);

    void pushConnectNodes(NodeItem* a, NodeItem* b);

    void pushFlipBranch(BranchItem* branch);

    void pushToggleTwoPortKind(TwoPortItem* item);

    void pushDeleteSelection();

    void undo();

    void redo();

    QUndoStack* undoStack() { return &m_undoStack; }



    QPointF snap(QPointF point) const;

    void setMode(Mode mode);

    Mode mode() const { return m_mode; }

    SystemType defaultSystemType() const { return m_defaultSystemType; }

    void setDefaultSystemType(SystemType type);

    bool snapToGrid() const { return m_snapToGrid; }

    void setSnapToGrid(bool enabled);

    bool showGrid() const { return m_showGrid; }

    void setShowGrid(bool enabled);

    qreal gridSpacing() const { return m_gridSpacing; }

    void setGridSpacing(qreal spacing);

    void refreshAppearance();



    NodeItem* createNode(const QPointF& center);

    void destroyNode(NodeItem* node);

    BranchItem* createBranch(NodeItem* a, NodeItem* b, qreal bow = 0.0);

    void destroyBranch(BranchItem* branch);

    void flipBranch(BranchItem* branch);

    TwoPortItem* createTwoPort(const QPointF& center, TwoPortKind kind,
                               const QString& modulus = QString(), const QString& name = QString());

    TwoPortItem* createTwoPortFromPorts(NodeItem* v1, NodeItem* v2, NodeItem* g1, NodeItem* g2,
                                        TwoPortKind kind, const QString& modulus = QString(),
                                        const QString& name = QString());

    void destroyTwoPort(TwoPortItem* item);

    void purgeTwoPort(TwoPortItem* item);



    struct BranchKey {

        QPointF from;

        QPointF to;

        int index;

    };

    struct TwoPortKey {

        QPointF center;

        TwoPortKind kind;

        QString modulus;

        QString name;

    };

    struct NodeSnapshot {

        QPointF pos;

        QString name;

        QString across;

        bool ground = false;

        SystemType systemType = SystemType::Mechanical;

    };

    struct BranchSnapshot {

        QPointF from;

        QPointF to;

        int index = 0;

        QString name;

        bool active = false;

        BranchType type = BranchType::A;

        QString constant = QStringLiteral("1");

        qreal bow = 0.0;

        int serialId = 0;

        int sourceInputId = 0;

    };



    void destroyBranchAt(const BranchKey& key);

    void pushMove(NodeItem* node, const QPointF& oldPos, const QPointF& newPos);

    void pushMoveTwoPort(TwoPortItem* item, const QPointF& oldCenter, const QPointF& newCenter);

    struct MergeUndoData {
        QPointF removePos;
        QString removeName;
        QString removeAcross;
        bool removeGround = false;
        TwoPortItem* twoPort = nullptr;
        int removeRole = 0;
        bool collapsedSharedRef = false;
        struct Rewire {
            BranchKey key;
            bool removeWasFrom = false;
        };
        struct Destroyed {
            BranchKey key;
            QString name;
            bool active = false;
            BranchType type = BranchType::A;
            QString constant;
            qreal bow = 0.0;
        };
        std::vector<Rewire> rewired;
        std::vector<Destroyed> destroyed;
    };

    void mergeNodes(NodeItem* keep, NodeItem* remove, MergeUndoData* undo = nullptr);

    void unmergeNodes(NodeItem* keep, const MergeUndoData& undo);

    void pushSetNodeName(NodeItem* node, const QString& name);

    void pushSetNodeAcrossVariable(NodeItem* node, const QString& symbol);

    void pushSetNodeGround(NodeItem* node, bool ground);

    void pushNodeProperties(NodeItem* node, bool oldGround, bool newGround, SystemType oldType,
                            SystemType newType);

    void pushSetBranchName(BranchItem* branch, const QString& name);

    void pushSetBranchActive(BranchItem* branch, bool active);

    void pushSetBranchType(BranchItem* branch, BranchType type);

    void pushSetBranchConstant(BranchItem* branch, const QString& constant);

    int allocateSourceInputId();

    void registerSourceInputId(int id);

    void pushSetNodeSystemType(NodeItem* node, SystemType type);

    void pushBranchProperties(BranchItem* branch, bool active, BranchType type, const QString& constant);

    void pushSetTwoPortName(TwoPortItem* item, const QString& name);

    void pushSetTwoPortKind(TwoPortItem* item, TwoPortKind kind);

    void pushSetTwoPortModulus(TwoPortItem* item, const QString& modulus);

    bool canMergeNodes(const NodeItem* a, const NodeItem* b, QString* reason = nullptr) const;

    bool pushMergeNodes(NodeItem* a, NodeItem* b, NodeItem* dragged = nullptr,
                        const QPointF& draggedOldPos = QPointF());

    bool tryMergeOverlappingNodes(NodeItem* moved, const QPointF& dragStart);

    QString takeLoadWarning();

    std::vector<BranchItem*> branchesBetween(NodeItem* a, NodeItem* b) const;

    TwoPortItem* twoPortAt(const QPointF& scenePos) const;

    TwoPortItem* twoPortAtCenter(const QPointF& center) const;

    TwoPortItem* twoPortFor(const QGraphicsItem* item) const;

    TwoPortItem* twoPortForNode(const NodeItem* node, TwoPortItem* prefer = nullptr,
                                const QPointF& sceneHint = QPointF()) const;

    void selectTwoPort(TwoPortItem* item);

    void selectTwoPortNode(NodeItem* node, TwoPortItem* twoPort = nullptr);

    static bool isInternalTwoPortBranch(TwoPortItem* twoPort, BranchItem* branch) {
        return twoPort && (branch == twoPort->leftBranch() || branch == twoPort->rightBranch());
    }

    static bool isTwoPortPortNode(const TwoPortItem* twoPort, const NodeItem* node) {
        return twoPort && node &&
               (node == twoPort->v1() || node == twoPort->v2() || node == twoPort->g1() ||
                node == twoPort->g2());
    }

    void captureDeleteState(std::vector<NodeSnapshot>& nodes, std::vector<BranchSnapshot>& branches,

                            std::vector<TwoPortKey>& twoPorts) const;

    void executeDelete(const std::vector<NodeSnapshot>& nodes, const std::vector<BranchSnapshot>& branches,

                       const std::vector<TwoPortKey>& twoPorts);

    void restoreDelete(const std::vector<NodeSnapshot>& nodes, const std::vector<BranchSnapshot>& branches,

                       const std::vector<TwoPortKey>& twoPorts);



    NodeItem* nodeAtPos(const QPointF& pos) const;

    QRectF contentBounds() const;

    lg::NormalTreeResult findNormalTree();

    lg::NormalTreeResult commitNormalTreeSelection(const std::vector<BranchItem*>& treeBranches);

    void beginManualNormalTreeSelection();

    void toggleManualNormalTreeBranch(BranchItem* branch);

    void setManualNormalTreeBranchRole(BranchItem* branch, bool inTree);

    void acceptManualNormalTreeSelection();

    void cancelManualNormalTreeSelection();

    const lg::NormalTreeResult& manualNormalTreeValidation() const {
        return m_manualNormalTreeValidation;
    }

    void clearNormalTreeHighlight();

    bool normalTreeHighlightActive() const { return m_normalTreeHighlightActive; }

    bool normalTreeIsManual() const { return m_normalTreeManual; }

    const lg::NormalTreeResult& lastNormalTreeResult() const { return m_lastNormalTreeResult; }

    struct SavedNormalTree {
        QString name;
        std::vector<int> treeBranchSerialIds;
    };

    const std::vector<SavedNormalTree>& savedNormalTrees() const { return m_savedNormalTrees; }

    int activeSavedNormalTreeIndex() const { return m_activeSavedNormalTreeIndex; }

    bool addSavedNormalTree(const QString& name);

    bool removeSavedNormalTree(int index);

    bool applySavedNormalTree(int index);

    QString savedNormalTreeListLabel(int index) const;

    void setSavedNormalTreesState(const std::vector<SavedNormalTree>& trees, int activeIndex);

    void pushSavedNormalTreesUndo(const std::vector<SavedNormalTree>& before, int beforeActive,
                                  const std::vector<SavedNormalTree>& after, int afterActive);

    lg::StateSpaceResult computeStateSpaceRep();

    const lg::StateSpaceResult& lastStateSpaceResult() const { return m_lastStateSpaceResult; }

    void clearDocument();

    QByteArray documentToJson() const;

    bool documentFromJson(const QByteArray& data, QString* error = nullptr);



    static constexpr qreal kTwoPortHalfWidth = 40.0;

    static constexpr qreal kTwoPortHalfHeight = 60.0;



signals:

    void modeChanged(Mode mode);

    void graphChanged();

    void normalTreeHighlightChanged();

    void savedNormalTreesChanged();

    void manualNormalTreeValidationChanged(const lg::NormalTreeResult& result);

    void manualNormalTreeAccepted(const lg::NormalTreeResult& result);

    void manualNormalTreeRejected(const QString& message);



protected:

    void drawBackground(QPainter* painter, const QRectF& rect) override;

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;



private:

    void reindexBranches(NodeItem* a, NodeItem* b);

    void clearBranchPending();

    BranchItem* branchAt(const QPointF& scenePos) const;

    NodeItem* nodeAt(const QPointF& scenePos, const NodeItem* except = nullptr) const;

    Mode m_mode = Mode::Select;

    SystemType m_defaultSystemType = SystemType::Mechanical;

    bool m_snapToGrid = true;

    bool m_showGrid = true;

    qreal m_gridSpacing = 20.0;

    NodeItem* m_pending = nullptr;

    QUndoStack m_undoStack;

    int m_nextNodeId = 1;

    int m_nextBranchId = 1;

    int m_nextSourceInputId = 1;

    int m_nextTwoPortId = 1;

    bool m_suppressGraphChange = false;

    QString m_loadWarning;

    bool m_normalTreeHighlightActive = false;

    bool m_normalTreeManual = false;

    lg::NormalTreeResult m_lastNormalTreeResult;

    lg::StateSpaceResult m_lastStateSpaceResult;

    struct ManualNormalTreeBackup {
        bool highlightActive = false;
        lg::NormalTreeResult result;
    };

    ManualNormalTreeBackup m_manualNormalTreeBackup;

    lg::NormalTreeResult m_manualNormalTreeValidation;

    std::vector<SavedNormalTree> m_savedNormalTrees;

    int m_activeSavedNormalTreeIndex = -1;

    BranchItem* branchBySerialId(int serialId) const;

    std::vector<int> currentTreeBranchSerialIds() const;

    void syncActiveSavedNormalTreeIndex();

    void leaveManualNormalTreeMode(bool accept);

    void restoreManualNormalTreeBackup();

    void refreshManualNormalTreeValidation();

    void setNormalTreePickingPassthrough(bool enabled);

    void collectGraphItems(std::vector<NodeItem*>& nodes, std::vector<BranchItem*>& branches,
                           std::vector<TwoPortItem*>& twoPorts) const;

    void notifyGraphChanged();

};



class GraphView : public QGraphicsView {

public:

    explicit GraphView(GraphScene* scene, QWidget* parent = nullptr);



    void setToolMode(GraphScene::Mode mode);

    void goHome();

    void zoomIn();

    void zoomOut();



protected:

    void wheelEvent(QWheelEvent* event) override;



private:

    void zoomBy(qreal factor, const QPoint& anchor);

};


