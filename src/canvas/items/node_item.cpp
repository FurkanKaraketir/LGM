#include "canvas.h"
#include "geometry.h"
#include "item_includes.h"

#include "elemental_equation.h"

#include <QGraphicsScene>
#include <QStyleOptionGraphicsItem>

using namespace ci;

NodeItem::NodeItem(qreal radius) : QGraphicsEllipseItem(-radius, -radius, radius * 2, radius * 2) {
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemSendsGeometryChanges);
    setZValue(1);
    refreshTheme();
}

void NodeItem::refreshTheme() {
    setBrush(itemFill(nullptr));
    setPen(QPen(itemInk(nullptr), 2));
    update();
}

void NodeItem::setAcrossVariable(const QString& symbol) {
    if (m_acrossVariable == symbol) {
        return;
    }
    prepareGeometryChange();
    m_acrossVariable = symbol;
    update();
}

void NodeItem::setGround(bool ground) {
    prepareGeometryChange();
    m_ground = ground;
    update();
}

void NodeItem::setSystemType(SystemType type) {
    if (m_systemType == type) {
        return;
    }
    m_systemType = type;
    lg::applyNodeDomainNaming(this);
    update();
}

void NodeItem::setTwoPort(TwoPortItem* twoPort) {
    m_twoPort = twoPort;
    setZValue(twoPort ? 3.0 : 1.0);
}

QRectF NodeItem::boundingRect() const {
    QRectF r = QGraphicsEllipseItem::boundingRect();
    if (m_ground) {
        // ground symbol extends: radius+2 stem + 8 down + 14 for three rungs = radius+16 below center
        const qreal extra = rect().width() / 2.0 + 18.0;
        r.setBottom(r.bottom() + extra);
    }
    QFont font;
    font.setPointSizeF(9.0);
    r |= nodeAcrossLabelRect(acrossVariable(), rect().width() / 2.0, font);
    return r;
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    QGraphicsEllipseItem::paint(painter, option, widget);
    if (m_ground) {
        painter->save();
        painter->setPen(QPen(itemInk(widget), 1.5));
        painter->setBrush(Qt::NoBrush);
        drawGroundSymbol(painter, QPointF(0.0, 0.0), rect().width() / 2.0);
        painter->restore();
    }
    const qreal radius = rect().width() / 2.0;
    const QString label = acrossVariable();
    QFont font = painter->font();
    font.setPointSizeF(9.0);
    painter->setFont(font);
    painter->setPen(isSelected() ? kSelectionColor : itemInk(widget));
    const QRectF textRect = nodeAcrossLabelRect(label, radius, font);
    painter->save();
    painter->setClipping(false);
    painter->drawText(textRect, Qt::AlignCenter, label);
    painter->restore();
}

void NodeItem::addBranch(BranchItem* branch) {
    m_branches.push_back(branch);
}

void NodeItem::removeBranch(BranchItem* branch) {
    auto it = std::find(m_branches.begin(), m_branches.end(), branch);
    if (it != m_branches.end()) {
        m_branches.erase(it);
    }
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemSelectedHasChanged) {
        update();
    }
    if (change == ItemPositionChange && scene()) {
        const auto* graph = static_cast<GraphScene*>(scene());
        return graph->snap(value.toPointF());
    }
    if (change == ItemPositionHasChanged) {
        for (BranchItem* branch : m_branches) {
            branch->updatePath();
        }
        if (m_twoPort) {
            m_twoPort->refresh();
        }
    }
    return QGraphicsEllipseItem::itemChange(change, value);
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    m_dragStart = pos();
    if (m_twoPort && scene()) {
        auto* graph = static_cast<GraphScene*>(scene());
        if (graph->mode() == GraphScene::Mode::SelectNormalTree) {
            event->accept();
            return;
        }
        if (graph->mode() == GraphScene::Mode::Select) {
            if (!(event->modifiers() & Qt::ShiftModifier)) {
                graph->selectTwoPortNode(this, graph->twoPortForNode(this, nullptr, event->scenePos()));
            } else {
                setSelected(true);
            }
            event->accept();
            return;
        }
    }
    QGraphicsEllipseItem::mousePressEvent(event);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsEllipseItem::mouseReleaseEvent(event);
    if (!scene()) {
        return;
    }
    auto* graph = static_cast<GraphScene*>(scene());
    if (graph->mode() == GraphScene::Mode::Select && m_dragStart != pos()) {
        if (!graph->tryMergeOverlappingNodes(this, m_dragStart)) {
            graph->pushMove(this, m_dragStart, pos());
        }
    }
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (!scene() || m_twoPort) {
        QGraphicsEllipseItem::mouseDoubleClickEvent(event);
        return;
    }
    if (static_cast<GraphScene*>(scene())->mode() == GraphScene::Mode::SelectNormalTree) {
        event->accept();
        return;
    }
    auto* dialog = new QDialog(scene()->views().first());
    dialog->setWindowTitle("Node Properties");
    auto* layout = new QVBoxLayout(dialog);
    
    layout->addWidget(new QLabel("Node Type:"));
    auto* normalRadio = new QRadioButton("Normal Node");
    auto* groundRadio = new QRadioButton("Reference (0-Ground) Node");
    normalRadio->setChecked(!m_ground);
    groundRadio->setChecked(m_ground);
    layout->addWidget(normalRadio);
    layout->addWidget(groundRadio);

    QComboBox* systemTypeCombo = nullptr;
    if (!m_ground) {
        layout->addWidget(new QLabel("System Type:"));
        systemTypeCombo = new QComboBox();
        systemTypeCombo->addItem("Mechanical (Translational)", static_cast<int>(SystemType::Mechanical));
        systemTypeCombo->addItem("Mechanical (Rotational)", static_cast<int>(SystemType::MechanicalRotational));
        systemTypeCombo->addItem("Electrical", static_cast<int>(SystemType::Electrical));
        systemTypeCombo->addItem("Fluid", static_cast<int>(SystemType::Fluid));
        systemTypeCombo->addItem("Heat", static_cast<int>(SystemType::Heat));
        systemTypeCombo->setCurrentIndex(systemTypeCombo->findData(static_cast<int>(m_systemType)));
        layout->addWidget(systemTypeCombo);
    }
    
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    
    if (dialog->exec() == QDialog::Accepted) {
        auto* graph = static_cast<GraphScene*>(scene());
        const bool newGround = groundRadio->isChecked();
        const SystemType newSystemType =
            systemTypeCombo && !newGround
                ? static_cast<SystemType>(systemTypeCombo->currentData().toInt())
                : m_systemType;
        graph->pushNodeProperties(this, m_ground, newGround, m_systemType, newSystemType);
    }
    dialog->deleteLater();
    event->accept();
}

