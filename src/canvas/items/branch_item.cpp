#include "canvas.h"
#include "geometry.h"
#include "item_includes.h"

#include "elemental_equation.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPalette>
#include <QSet>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>

using namespace ci;

BranchItem::BranchItem(NodeItem* from, NodeItem* to, int index, int count, qreal bow)
    : m_from(from), m_to(to), m_index(index), m_count(count), m_bow(bow) {
    setBrush(Qt::NoBrush);
    setFlags(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemClipsToShape, false);
    setZValue(0);
    refreshTheme();
    updatePath();
}

void BranchItem::setSlot(int index, int count) {
    m_index = index;
    m_count = count;
    updatePath();
}

void BranchItem::setBow(qreal bow) {
    m_bow = bow;
    updatePath();
}

void BranchItem::updatePath() {
    if (!m_from || !m_to || !m_from->scene() || !m_to->scene()) {
        return;
    }
    prepareGeometryChange();
    const QPointF a = m_from->scenePos();
    const QPointF b = m_to->scenePos();
    const bool dashedTail = !m_active && m_type == BranchType::A;
    const bool drawArrow = !isTwoPortPortBranch(this);
    if (std::abs(m_bow) > 1e-6) {
        setPath(bowedBranch(a, b, m_bow, m_active, dashedTail, drawArrow));
    } else {
        const BranchEgress egress = computeBranchEgress(this);
        setPath(fieldLineBranch(a, b, m_index, m_count, m_active, dashedTail, drawArrow, egress.kickA,
                                egress.kickB));
    }
}

void BranchItem::setActive(bool active) {
    m_active = active;
    if (active && m_type == BranchType::D) {
        m_type = BranchType::A;
    }
    if (active && (m_type == BranchType::A || m_type == BranchType::T) && m_sourceInputId == 0) {
        const int parsed = lg::parseSourceInputIdFromName(m_name);
        if (parsed > 0) {
            m_sourceInputId = parsed;
        } else if (auto* graphScene = qobject_cast<GraphScene*>(scene())) {
            m_sourceInputId = graphScene->allocateSourceInputId();
        } else {
            m_sourceInputId = 1;
        }
        if (auto* graphScene = qobject_cast<GraphScene*>(scene())) {
            graphScene->registerSourceInputId(m_sourceInputId);
        }
    }
    lg::applySourceThroughNaming(this);
    updatePath();
}

void BranchItem::setBranchType(BranchType type) {
    m_type = type;
    lg::applySourceThroughNaming(this);
    updatePath();
}

void BranchItem::flip() {
    std::swap(m_from, m_to);
    m_bow = -m_bow;
    updatePath();
}

void BranchItem::replaceEndpoint(NodeItem* oldNode, NodeItem* newNode) {
    if (!oldNode || !newNode || oldNode == newNode) {
        return;
    }
    if (m_from == oldNode) {
        oldNode->removeBranch(this);
        m_from = newNode;
        newNode->addBranch(this);
    }
    if (m_to == oldNode) {
        oldNode->removeBranch(this);
        m_to = newNode;
        newNode->addBranch(this);
    }
    updatePath();
}

QString BranchItem::elementalEquationText() const {
    return lg::elementalEquationText(m_type, lg::branchSystemType(*this), !m_active, m_elementConstant);
}

void BranchItem::setElementConstant(const QString& constant) {
    if (m_elementConstant == constant) {
        return;
    }
    prepareGeometryChange();
    m_elementConstant = constant;
    lg::applyConstantThroughNaming(this);
    update();
}

void BranchItem::setNormalTreeRole(bool inTree, bool known) {
    m_inNormalTree = inTree;
    m_normalTreeRoleKnown = known;
    updateBranchPen();
}

void BranchItem::updateBranchPen() {
    QPen pen;
    if (isSelected()) {
        pen = QPen(kSelectionColor, 2);
    } else if (m_normalTreeRoleKnown && m_inNormalTree) {
        pen = QPen(kNormalTreeColor, 3);
    } else if (m_normalTreeRoleKnown) {
        pen = QPen(kCotreeColor, 1.5);
    } else {
        pen = QPen(itemInk(nullptr), 2);
    }
    setPen(pen);
}

void BranchItem::refreshTheme() {
    updateBranchPen();
    update();
}

QRectF BranchItem::boundingRect() const {
    QRectF rect = QGraphicsPathItem::boundingRect();
    if (!m_from || !m_to || !m_from->scene() || !m_to->scene()) {
        return rect;
    }
    const QPointF a = m_from->scenePos();
    const QPointF b = m_to->scenePos();
    const BranchEgress egress = computeBranchEgress(this);
    const BranchArrowGeom arrow = std::abs(m_bow) > 1e-6
                                      ? branchArrowGeom(a, b, m_index, m_count, m_bow, egress.kickA, egress.kickB)
                                      : fieldLineArrowGeom(a, b, m_index, m_count, egress.kickA, egress.kickB);
    const QString label = branchAnnotationLabel(this);
    QFont font;
    font.setPointSizeF(9.0);
    rect |= constantLabelRect(arrow.tip, arrow.tangent, label, font);
    return rect.marginsAdded(QMarginsF(2.0, 2.0, 2.0, 2.0));
}

QPainterPath BranchItem::shape() const {
    return strokedPickShape(path());
}

void BranchItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    QGraphicsPathItem::paint(painter, option, widget);

    if (!m_from || !m_to || !m_from->scene() || !m_to->scene()) {
        return;
    }
    if (isTwoPortPortBranch(this)) {
        return;
    }

    const QPointF a = m_from->scenePos();
    const QPointF b = m_to->scenePos();
    const BranchEgress egress = computeBranchEgress(this);
    const BranchArrowGeom arrow = std::abs(m_bow) > 1e-6
                                      ? branchArrowGeom(a, b, m_index, m_count, m_bow, egress.kickA, egress.kickB)
                                      : fieldLineArrowGeom(a, b, m_index, m_count, egress.kickA, egress.kickB);

    painter->save();
    painter->setClipping(false);
    if (!m_active) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(pen().color());
        painter->fillPath(filledArrowHead(arrow.tip, arrow.tangent), pen().color());
    }
    painter->restore();

    const QString label = branchAnnotationLabel(this);
    QFont font = painter->font();
    font.setPointSizeF(9.0);
    painter->setFont(font);
    painter->setPen(isSelected() ? kSelectionColor : itemInk(widget));

    const QRectF textRect = constantLabelRect(arrow.tip, arrow.tangent, label, font);
    painter->save();
    painter->setClipping(false);
    painter->drawText(textRect, Qt::AlignCenter, label);
    painter->restore();
}

void BranchItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (!shape().contains(event->pos())) {
        event->ignore();
        return;
    }
    if (scene() && event->button() == Qt::LeftButton) {
        auto* graph = static_cast<GraphScene*>(scene());
        if (graph->mode() == GraphScene::Mode::SelectNormalTree) {
            graph->toggleManualNormalTreeBranch(this);
            event->accept();
            return;
        }
    }
    QGraphicsPathItem::mousePressEvent(event);
}

void BranchItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (!scene()) {
        return;
    }
    if (static_cast<GraphScene*>(scene())->mode() == GraphScene::Mode::SelectNormalTree) {
        event->accept();
        return;
    }
    if (isTwoPortPortBranch(this)) {
        event->accept();
        return;
    }
    auto* dialog = new QDialog(scene()->views().first());
    dialog->setWindowTitle("Branch Properties");
    auto* layout = new QVBoxLayout(dialog);
    
    layout->addWidget(new QLabel("Branch Category:"));
    auto* categoryCombo = new QComboBox();
    categoryCombo->addItem("Passive Element", false);
    categoryCombo->addItem("Active Element (Source)", true);
    categoryCombo->setCurrentIndex(m_active ? 1 : 0);
    layout->addWidget(categoryCombo);
    
    auto* typeLabel = new QLabel("Element Type:");
    layout->addWidget(typeLabel);
    auto* typeCombo = new QComboBox();
    layout->addWidget(typeCombo);
    
    auto updateTypeOptions = [=]() {
        const bool isActive = categoryCombo->currentData().toBool();
        typeCombo->clear();
        if (isActive) {
            typeCombo->addItem("A-type", static_cast<int>(BranchType::A));
            typeCombo->addItem("T-type", static_cast<int>(BranchType::T));
            int currentIdx = (m_type == BranchType::T) ? 1 : 0;
            typeCombo->setCurrentIndex(currentIdx);
        } else {
            typeCombo->addItem("A-type", static_cast<int>(BranchType::A));
            typeCombo->addItem("T-type", static_cast<int>(BranchType::T));
            typeCombo->addItem("D-type", static_cast<int>(BranchType::D));
            int currentIdx = 0;
            switch (m_type) {
                case BranchType::A: currentIdx = 0; break;
                case BranchType::T: currentIdx = 1; break;
                case BranchType::D: currentIdx = 2; break;
            }
            typeCombo->setCurrentIndex(currentIdx);
        }
    };
    
    QObject::connect(categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateTypeOptions);
    updateTypeOptions();

    auto* constantLabel = new QLabel("Constant:");
    auto* constantEdit = new QLineEdit(m_elementConstant);
    constantEdit->setPlaceholderText(QStringLiteral("e.g. R1, C2, M3"));
    layout->addWidget(constantLabel);
    layout->addWidget(constantEdit);

    auto refreshConstantVisibility = [=]() {
        const bool passive = !categoryCombo->currentData().toBool();
        constantLabel->setVisible(passive);
        constantEdit->setVisible(passive);
    };
    QObject::connect(categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), refreshConstantVisibility);
    QObject::connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), refreshConstantVisibility);
    refreshConstantVisibility();
    
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=]() {
        if (!categoryCombo->currentData().toBool() &&
            !lg::isValidElementConstant(constantEdit->text())) {
            constantEdit->setFocus();
            constantEdit->selectAll();
            return;
        }
        dialog->accept();
    });

    if (dialog->exec() == QDialog::Accepted) {
        auto* graph = static_cast<GraphScene*>(scene());
        graph->pushBranchProperties(
            this, categoryCombo->currentData().toBool(),
            static_cast<BranchType>(typeCombo->currentData().toInt()),
            constantEdit->text());
    }
    dialog->deleteLater();
    event->accept();
}

QVariant BranchItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemSelectedHasChanged) {
        updateBranchPen();
    }
    return QGraphicsPathItem::itemChange(change, value);
}

