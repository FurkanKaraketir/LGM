#pragma once



#include <QGraphicsEllipseItem>

#include <QGraphicsItem>

#include <QGraphicsPathItem>

#include <QGraphicsScene>

#include <QGraphicsView>

#include <QString>

#include <QUndoStack>

#include <vector>



class QGraphicsSceneMouseEvent;

class GraphScene;

class NodeItem;
class TwoPortItem;



enum class TwoPortKind { Transformer, Gyrator };

enum class BranchType { A, T, D };



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



protected:

    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;



private:

    NodeItem* m_from;

    NodeItem* m_to;

    int m_index;

    int m_count;

    qreal m_bow;

    bool m_active = false;

    BranchType m_type = BranchType::A;

    QString m_name;

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

    void setTwoPort(TwoPortItem* twoPort) { m_twoPort = twoPort; }

    QString name() const { return m_name; }

    void setName(const QString& name) { m_name = name; }



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



    void refresh();

    void moveBy(const QPointF& delta);

    bool isSyncing() const { return m_syncing; }

    void selectMembers(bool selected);



    QRectF boundingRect() const override;

    QPainterPath shape() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;



private:

    QPointF couplerLeft() const;

    QPointF couplerRight() const;



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
};



class GraphScene : public QGraphicsScene {

    Q_OBJECT



public:

    enum class Mode { Select, AddNode, AddBranch, AddTwoPort, Delete };



    explicit GraphScene(QObject* parent = nullptr);



    void pushAddNode(const QPointF& center);

    void pushAddTwoPort(const QPointF& center, TwoPortKind kind = TwoPortKind::Transformer);

    void pushConnectNodes(NodeItem* a, NodeItem* b);

    void pushToggleTwoPortKind(TwoPortItem* item);

    void pushDeleteAt(const QPointF& scenePos);

    void undo();

    void redo();

    QUndoStack* undoStack() { return &m_undoStack; }



    QPointF snap(QPointF point) const;

    void setMode(Mode mode);

    Mode mode() const { return m_mode; }



    NodeItem* createNode(const QPointF& center);

    void destroyNode(NodeItem* node);

    BranchItem* createBranch(NodeItem* a, NodeItem* b, qreal bow = 0.0);

    void destroyBranch(BranchItem* branch);

    TwoPortItem* createTwoPort(const QPointF& center, TwoPortKind kind);

    void destroyTwoPort(TwoPortItem* item);



    struct BranchKey {

        QPointF from;

        QPointF to;

        int index;

    };

    struct TwoPortKey {

        QPointF center;

        TwoPortKind kind;

    };



    void destroyBranchAt(const BranchKey& key);

    void pushMove(NodeItem* node, const QPointF& oldPos, const QPointF& newPos);

    std::vector<BranchItem*> branchesBetween(NodeItem* a, NodeItem* b) const;

    TwoPortItem* twoPortAt(const QPointF& scenePos) const;

    TwoPortItem* twoPortAtCenter(const QPointF& center) const;

    TwoPortItem* twoPortFor(const QGraphicsItem* item) const;

    void selectTwoPort(TwoPortItem* item);

    void captureDeleteState(std::vector<QPointF>& nodes, std::vector<BranchKey>& branches,

                            std::vector<TwoPortKey>& twoPorts) const;

    void executeDelete(const std::vector<QPointF>& nodes, const std::vector<BranchKey>& branches,

                       const std::vector<TwoPortKey>& twoPorts);

    void restoreDelete(const std::vector<QPointF>& nodes, const std::vector<BranchKey>& branches,

                       const std::vector<TwoPortKey>& twoPorts);



    NodeItem* nodeAtPos(const QPointF& pos) const;

    QRectF contentBounds() const;



    static constexpr qreal kTwoPortHalfWidth = 40.0;

    static constexpr qreal kTwoPortHalfHeight = 60.0;



signals:

    void modeChanged(Mode mode);

    void graphChanged();



protected:

    void drawBackground(QPainter* painter, const QRectF& rect) override;

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;



private:

    void reindexBranches(NodeItem* a, NodeItem* b);

    static bool isInternalTwoPortBranch(TwoPortItem* twoPort, BranchItem* branch) {
        return twoPort && (branch == twoPort->leftBranch() || branch == twoPort->rightBranch());
    }

    void clearBranchPending();

    BranchItem* branchAt(const QPointF& scenePos) const;

    NodeItem* nodeAt(const QPointF& scenePos) const;

    static constexpr qreal kGrid = 20.0;



    Mode m_mode = Mode::Select;

    NodeItem* m_pending = nullptr;

    QUndoStack m_undoStack;

    int m_nextNodeId = 1;

    int m_nextBranchId = 1;

    int m_nextTwoPortId = 1;

    bool m_suppressGraphChange = false;

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

    void keyPressEvent(QKeyEvent* event) override;

    void wheelEvent(QWheelEvent* event) override;



private:

    void zoomBy(qreal factor, const QPoint& anchor);

};


