#include "mainwindow.h"
#include "detail.h"
#include "common_includes.h"

using namespace mw;

#include "canvas.h"
#include "elemental_equation.h"
#include "normal_tree.h"

#include <QComboBox>
#include <QGraphicsItem>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>

void MainWindow::updatePropertyPanel() {

    if (m_clearingDocument) {

        return;

    }

    m_updatingPropertyPanel = true;

    m_propertyTable->setRowCount(0);

    m_propertyTargetPtr = nullptr;

    m_propertyTargetKind = -1;



    auto addRow = [this](const QString& property, const QString& value, bool editable = false) {

        const int row = m_propertyTable->rowCount();

        m_propertyTable->insertRow(row);

        auto* nameItem = new QTableWidgetItem(property);

        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);

        m_propertyTable->setItem(row, 0, nameItem);

        auto* valueItem = new QTableWidgetItem(value);

        if (!editable) {

            valueItem->setFlags(valueItem->flags() & ~Qt::ItemIsEditable);

        }

        m_propertyTable->setItem(row, 1, valueItem);

    };



    auto addLabelRow = [this](const QString& property) {

        const int row = m_propertyTable->rowCount();

        m_propertyTable->insertRow(row);

        auto* nameItem = new QTableWidgetItem(property);

        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);

        m_propertyTable->setItem(row, 0, nameItem);

        return row;

    };



    auto appendNormalTreeAnalysis = [this, &addRow](const lg::NormalTreeResult& normalTree) {

        addRow(tr("Analysis"), tr("Normal tree"));

        if (m_scene->mode() == GraphScene::Mode::SelectNormalTree) {
            addRow(tr("Source"), tr("Manual selection (editing)"));
        } else if (m_scene->normalTreeIsManual()) {
            addRow(tr("Source"), tr("Manual selection"));
        } else {
            addRow(tr("Source"), tr("Auto-detected"));
        }

        addRow(tr("Tree branches"), QString::number(normalTree.treeBranches.size()));

        addRow(tr("System order"), QString::number(normalTree.stateVariables.size()));

        if (normalTree.stateVariables.empty()) {

            addRow(tr("State variables"), tr("none"));

            return;

        }

        for (int i = 0; i < static_cast<int>(normalTree.stateVariables.size()); ++i) {

            const lg::NormalTreeResult::StateVariable& state =
                normalTree.stateVariables[static_cast<size_t>(i)];

            const QString label = state.kind == lg::NormalTreeResult::StateVariable::Kind::Across
                                      ? tr("x%1 (across)").arg(i + 1)
                                      : tr("x%1 (through)").arg(i + 1);

            addRow(label, state.symbol);

        }

    };



    QVector<GraphSelectionEntry> entries = primarySceneSelection(m_scene);

    if (entries.isEmpty()) {

        for (QTreeWidgetItem* item : m_objectTree->selectedItems()) {

            if (objectPtr(item)) {

                entries.push_back({objectPtr(item), objectKind(item)});

            }

        }

    }



    void* ptr = nullptr;

    GraphObjectKind kind = GraphObjectKind::Node;



    if (m_scene->normalTreeHighlightActive()) {

        appendNormalTreeAnalysis(m_scene->lastNormalTreeResult());

    }

    if (m_scene->mode() == GraphScene::Mode::SelectNormalTree) {
        int inTreeCount = 0;
        for (QGraphicsItem* item : m_scene->items()) {
            if (auto* branch = dynamic_cast<BranchItem*>(item)) {
                if (branch->normalTreeRoleKnown() && branch->inNormalTree()) {
                    ++inTreeCount;
                }
            }
        }
        const lg::NormalTreeResult& validation = m_scene->manualNormalTreeValidation();
        const bool valid = validation.status == lg::NormalTreeResult::Status::Ok;
        addRow(tr("Selection mode"), tr("Editing normal tree"));
        addRow(tr("Branches in tree"), QString::number(inTreeCount));
        addRow(tr("Validation"),
               valid ? tr("Valid")
                     : (validation.message.isEmpty() ? tr("Invalid") : validation.message));

        const int actionsRow = addLabelRow(tr("Actions"));
        auto* actions = new QWidget(m_propertyTable);
        auto* actionsLayout = new QHBoxLayout(actions);
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        auto* applyBtn = new QPushButton(tr("Apply"), actions);
        auto* cancelBtn = new QPushButton(tr("Cancel"), actions);
        applyBtn->setEnabled(valid);
        actionsLayout->addWidget(applyBtn);
        actionsLayout->addWidget(cancelBtn);
        connect(applyBtn, &QPushButton::clicked, this, [this]() {
            m_scene->acceptManualNormalTreeSelection();
            m_view->setToolMode(GraphScene::Mode::Select);
        });
        connect(cancelBtn, &QPushButton::clicked, this, [this]() {
            m_scene->cancelManualNormalTreeSelection();
            m_view->setToolMode(GraphScene::Mode::Select);
            statusBar()->showMessage(tr("Manual normal tree selection cancelled."), 3000);
            updatePropertyPanel();
        });
        m_propertyTable->setCellWidget(actionsRow, 1, actions);
        addRow(tr("Hint"), tr("Click branches to toggle; Enter to apply, Esc to cancel"));
    }



    if (entries.size() > 1) {

        addRow(tr("Selection"), tr("%1 objects selected").arg(entries.size()));

        QStringList names;

        names.reserve(entries.size());

        for (const GraphSelectionEntry& entry : entries) {

            names.push_back(objectLabel(entry.kind, entry.ptr));

        }

        addRow(tr("Objects"), names.join(QStringLiteral(", ")));

        m_updatingPropertyPanel = false;

        updateFlipBranchAction();

        return;

    }



    if (entries.size() == 1) {

        ptr = entries.front().ptr;

        kind = entries.front().kind;

    }



    if (ptr) {

        m_propertyTargetPtr = ptr;

        m_propertyTargetKind = static_cast<int>(kind);



        switch (kind) {

        case GraphObjectKind::Node: {

            auto* node = static_cast<NodeItem*>(ptr);

            addRow(tr("Name"), node->name(), true);

            if (node->isGround()) {
                addRow(tr("Across variable"), node->acrossVariable());
            } else {
                const int acrossRow = addLabelRow(tr("Across variable"));
                auto* acrossEdit = new QLineEdit(node->acrossVariable(), m_propertyTable);
                connect(acrossEdit, &QLineEdit::editingFinished, this, [this, node, acrossEdit]() {
                    if (m_updatingPropertyPanel) {
                        return;
                    }
                    const QString symbol = acrossEdit->text().trimmed();
                    if (!lg::isValidVariableSymbol(symbol)) {
                        acrossEdit->setText(node->acrossVariable());
                        return;
                    }
                    m_scene->pushSetNodeAcrossVariable(node, symbol);
                });
                m_propertyTable->setCellWidget(acrossRow, 1, acrossEdit);
            }

            const int typeRow = addLabelRow(tr("Node Type"));

            auto* combo = new QComboBox(m_propertyTable);

            combo->addItem(tr("Normal Node"));

            combo->addItem(tr("Reference (Ground) Node"));

            combo->setCurrentIndex(node->isGround() ? 1 : 0);

            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,

                    [this, node](int index) {

                        if (m_updatingPropertyPanel) {

                            return;

                        }

                        m_scene->pushSetNodeGround(node, index == 1);

                    });

            m_propertyTable->setCellWidget(typeRow, 1, combo);

            const QPointF pos = node->scenePos();

            addRow(tr("Position"), QString("(%1, %2)").arg(pos.x(), 0, 'f', 0).arg(pos.y(), 0, 'f', 0));

            addRow(tr("Branches"), QString::number(node->branches().size()));

            if (TwoPortItem* parentTwoPort = m_scene->twoPortForNode(node)) {

                addRow(tr("Parent"), parentTwoPort->name());

            }

            if (!node->isGround()) {
                const int systemTypeRow = addLabelRow(tr("System type"));

                auto* systemTypeCombo = new QComboBox(m_propertyTable);

                systemTypeCombo->addItem(tr("Mechanical (Translational)"),
                                         static_cast<int>(SystemType::Mechanical));

                systemTypeCombo->addItem(tr("Mechanical (Rotational)"),
                                         static_cast<int>(SystemType::MechanicalRotational));

                systemTypeCombo->addItem(tr("Electrical"), static_cast<int>(SystemType::Electrical));

                systemTypeCombo->addItem(tr("Fluid"), static_cast<int>(SystemType::Fluid));

                systemTypeCombo->addItem(tr("Heat"), static_cast<int>(SystemType::Heat));

                systemTypeCombo->setCurrentIndex(
                    systemTypeCombo->findData(static_cast<int>(node->systemType())));

                connect(systemTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,

                        [this, node, systemTypeCombo](int index) {

                            if (m_updatingPropertyPanel) {

                                return;

                            }

                            m_scene->pushSetNodeSystemType(
                                node, static_cast<SystemType>(systemTypeCombo->itemData(index).toInt()));

                            syncDefaultSystemTypeCombo(
                                static_cast<SystemType>(systemTypeCombo->itemData(index).toInt()));

                            updatePropertyPanel();

                        });

                m_propertyTable->setCellWidget(systemTypeRow, 1, systemTypeCombo);
            }

            break;

        }

        case GraphObjectKind::Branch: {

            auto* branch = static_cast<BranchItem*>(ptr);

            addRow(tr("Through variable"), lg::branchThroughSymbol(*branch), true);

            addRow(tr("Across variable"), lg::branchAcrossVariableText(*branch));

            if (branch->isActive()) {

                addRow(tr("Input"), lg::branchSourceInputDisplay(*branch));

            }

            if (TwoPortItem* twoPort = m_scene->twoPortFor(branch);
                twoPort && GraphScene::isInternalTwoPortBranch(twoPort, branch)) {
                addRow(tr("Parent"), twoPort->name());
                addRow(tr("Through variable"), lg::branchThroughSymbol(*branch));
                addRow(tr("Element"), tr("T-type (port branch)"));
                addRow(tr("Elemental equation"), branch->elementalEquationText());
                addRow(tr("Two-port equations"), twoPort->elementalEquationText());
                addRow(tr("From"), branch->from()->name());
                addRow(tr("To"), branch->to()->name());
                break;
            }

            addRow(tr("System type"), lg::systemTypeLabel(lg::branchSystemType(*branch)));

            const int categoryRow = addLabelRow(tr("Category"));

            auto* categoryCombo = new QComboBox(m_propertyTable);

            categoryCombo->addItem(tr("Passive"), false);

            categoryCombo->addItem(tr("Active"), true);

            categoryCombo->setCurrentIndex(branch->isActive() ? 1 : 0);

            const int elementRow = addLabelRow(tr("Element"));

            auto* elementCombo = new QComboBox(m_propertyTable);

            auto refreshElementOptions = [elementCombo, branch]() {

                const bool isActive = branch->isActive();

                const BranchType current = branch->branchType();

                QSignalBlocker blocker(elementCombo);

                elementCombo->clear();

                elementCombo->addItem(tr("A-type"), static_cast<int>(BranchType::A));

                elementCombo->addItem(tr("T-type"), static_cast<int>(BranchType::T));

                if (!isActive) {

                    elementCombo->addItem(tr("D-type"), static_cast<int>(BranchType::D));

                }

                int idx = 0;

                switch (current) {

                case BranchType::A:

                    idx = 0;

                    break;

                case BranchType::T:

                    idx = 1;

                    break;

                case BranchType::D:

                    idx = isActive ? 0 : 2;

                    break;

                }

                elementCombo->setCurrentIndex(idx);

            };

            refreshElementOptions();

            connect(categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,

                    [this, branch, categoryCombo](int index) {

                        if (m_updatingPropertyPanel) {

                            return;

                        }

                        m_scene->pushSetBranchActive(branch, categoryCombo->itemData(index).toBool());

                    });

            connect(elementCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,

                    [this, branch, elementCombo](int index) {

                        if (m_updatingPropertyPanel) {

                            return;

                        }

                        m_scene->pushSetBranchType(

                            branch, static_cast<BranchType>(elementCombo->itemData(index).toInt()));

                    });

            m_propertyTable->setCellWidget(categoryRow, 1, categoryCombo);

            m_propertyTable->setCellWidget(elementRow, 1, elementCombo);

            if (m_scene->normalTreeHighlightActive() && branch->normalTreeRoleKnown()) {

                if (m_scene->mode() == GraphScene::Mode::SelectNormalTree) {
                    const int treeRoleRow = addLabelRow(tr("Normal tree"));
                    auto* treeRoleCombo = new QComboBox(m_propertyTable);
                    treeRoleCombo->addItem(tr("Tree branch"), true);
                    treeRoleCombo->addItem(tr("Link"), false);
                    treeRoleCombo->setCurrentIndex(branch->inNormalTree() ? 0 : 1);
                    if (branch->isActive() && branch->branchType() == BranchType::A) {
                        treeRoleCombo->setEnabled(false);
                    }
                    connect(treeRoleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                            [this, branch, treeRoleCombo](int index) {
                                if (m_updatingPropertyPanel) {
                                    return;
                                }
                                m_scene->setManualNormalTreeBranchRole(
                                    branch, treeRoleCombo->itemData(index).toBool());
                                updatePropertyPanel();
                            });
                    m_propertyTable->setCellWidget(treeRoleRow, 1, treeRoleCombo);
                } else {
                    addRow(tr("Normal tree"), branch->inNormalTree() ? tr("Tree branch") : tr("Link"));
                }

                if (!branch->isActive()) {
                    if (branch->inNormalTree() && branch->branchType() == BranchType::A) {
                        addRow(tr("State variable"),
                               lg::branchAcrossSymbol(*branch));
                    } else if (!branch->inNormalTree() && branch->branchType() == BranchType::T) {
                        addRow(tr("State variable"), lg::branchThroughSymbol(*branch));
                    }
                }

            }

            if (!branch->isActive()) {
                const QString equation = branch->elementalEquationText();
                if (!equation.isEmpty()) {
                    addRow(tr("Elemental equation"), equation);
                }

                const int constantRow = addLabelRow(tr("Constant"));

                auto* constantEdit = new QLineEdit(branch->elementConstant(), m_propertyTable);
                constantEdit->setPlaceholderText(tr("e.g. R1, C2, M3"));
                connect(constantEdit, &QLineEdit::editingFinished, this, [this, branch, constantEdit]() {

                    if (m_updatingPropertyPanel) {

                        return;

                    }

                    const QString text = constantEdit->text();
                    if (!lg::isValidElementConstant(text)) {
                        constantEdit->setText(branch->elementConstant());
                        return;
                    }

                    m_scene->pushSetBranchConstant(branch, text);
                    updatePropertyPanel();

                });

                m_propertyTable->setCellWidget(constantRow, 1, constantEdit);

            }

            addRow(tr("From"), branch->from()->name());

            addRow(tr("To"), branch->to()->name());

            const int flipRow = addLabelRow(tr("Orientation"));

            auto* flipBtn = new QPushButton(tr("Flip"), m_propertyTable);

            connect(flipBtn, &QPushButton::clicked, this, [this, branch]() {

                m_scene->pushFlipBranch(branch);

            });

            m_propertyTable->setCellWidget(flipRow, 1, flipBtn);

            break;

        }

        case GraphObjectKind::TwoPort: {

            auto* twoPort = static_cast<TwoPortItem*>(ptr);

            addRow(tr("Name"), twoPort->name(), true);

            const int kindRow = addLabelRow(tr("Kind"));

            auto* kindCombo = new QComboBox(m_propertyTable);

            kindCombo->addItem(tr("Transformer"), static_cast<int>(TwoPortKind::Transformer));

            kindCombo->addItem(tr("Gyrator"), static_cast<int>(TwoPortKind::Gyrator));

            kindCombo->setCurrentIndex(twoPort->kind() == TwoPortKind::Gyrator ? 1 : 0);

            connect(kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,

                    [this, twoPort, kindCombo](int index) {

                        if (m_updatingPropertyPanel) {

                            return;

                        }

                        m_scene->pushSetTwoPortKind(

                            twoPort, static_cast<TwoPortKind>(kindCombo->itemData(index).toInt()));

                        updatePropertyPanel();

                    });

            m_propertyTable->setCellWidget(kindRow, 1, kindCombo);

            const QString modulusLabel =
                twoPort->kind() == TwoPortKind::Transformer ? tr("Transformer ratio (TF)")
                                                            : tr("Gyrator modulus (GY)");
            const int modulusRow = addLabelRow(modulusLabel);
            auto* modulusEdit = new QLineEdit(twoPort->modulus(), m_propertyTable);
            connect(modulusEdit, &QLineEdit::editingFinished, this, [this, twoPort, modulusEdit]() {
                if (m_updatingPropertyPanel) {
                    return;
                }
                m_scene->pushSetTwoPortModulus(twoPort, modulusEdit->text());
                updatePropertyPanel();
            });
            m_propertyTable->setCellWidget(modulusRow, 1, modulusEdit);

            addRow(tr("Elemental equations"), twoPort->elementalEquationText());

            if (twoPort->hasSharedReference()) {
                addRow(tr("Reference"), tr("Shared (3-terminal)"));
            }

            addRow(tr("Port 1"), QStringLiteral("v\u2081 = %1, f\u2081 = %2")
                                      .arg(twoPort->v1()->acrossVariable(), twoPort->leftBranch()->name()));
            addRow(tr("Port 2"), QStringLiteral("v\u2082 = %1, f\u2082 = %2")
                                      .arg(twoPort->v2()->acrossVariable(), twoPort->rightBranch()->name()));

            const QPointF center = twoPort->center();

            addRow(tr("Center"), QString("(%1, %2)").arg(center.x(), 0, 'f', 0).arg(center.y(), 0, 'f', 0));

            break;

        }

        }

    } else if (m_objectTree->topLevelItemCount() == 0) {

        addRow(tr("Type"), tr("Graph Scene"));

        addRow(tr("Zoom"), QString::number(m_view->transform().m11(), 'f', 2));

        addRow(tr("Grid"), tr("Enabled"));

    } else {

        addRow(tr("Selection"), tr("Nothing selected"));

    }

    m_updatingPropertyPanel = false;

    updateFlipBranchAction();

}
