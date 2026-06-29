#include "analyze_window.h"

#include "canvas.h"
#include "latex_widget.h"
#include "normal_tree.h"
#include "trademarks.h"

#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QAbstractItemView>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace lg {

void exportStateSpaceMatlabScript(QWidget* parent, const StateSpaceResult& stateSpace) {
    const QString matlab = app::matlabRegistered();
    const QString path = QFileDialog::getSaveFileName(
        parent, QObject::tr("Export %1 Script").arg(matlab), QStringLiteral("model.m"),
        QObject::tr("%1 Script (*.m);;All Files (*)").arg(matlab));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(parent, QObject::tr("Export %1 Script").arg(matlab),
                             QObject::tr("Could not write %1.").arg(path));
        return;
    }
    file.write(generateMatlabScript(stateSpace).toUtf8());
}

void populateStateSpaceLayout(QVBoxLayout* layout, QWidget* parent, const StateSpaceResult& stateSpace) {
    auto addSection = [layout, parent](const QString& title, const QStringList& lines) {
        if (lines.isEmpty()) {
            return;
        }
        auto* header = new QLabel(title, parent);
        QFont font = header->font();
        font.setBold(true);
        header->setFont(font);
        layout->addWidget(header);
        for (const QString& line : lines) {
            auto* label = new QLabel(line, parent);
            label->setWordWrap(false);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            layout->addWidget(label);
        }
    };

    if (stateSpace.status != StateSpaceResult::Status::Ok) {
        auto* hint = new QLabel(
            stateSpace.message.isEmpty() ? QObject::tr("No state-space result yet.") : stateSpace.message,
            parent);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        layout->addStretch();
        return;
    }

    if (!stateSpace.inputs.isEmpty()) {
        const QString inputSummary =
            stateSpace.inputLabels.isEmpty() ? stateSpace.inputs.join(QStringLiteral(", "))
                                             : stateSpace.inputLabels.join(QStringLiteral(", "));
        addSection(QObject::tr("Inputs"), {inputSummary});
    }
    addSection(QObject::tr("Elemental equations"), stateSpace.elementalEquations);
    addSection(QObject::tr("Continuity equations"), stateSpace.continuityEquations);
    addSection(QObject::tr("Compatibility equations"), stateSpace.compatibilityEquations);
    addSection(QObject::tr("State equations"), stateSpace.stateEquations);

    if (!stateSpace.matrixForm.isEmpty()) {
        auto* header = new QLabel(QObject::tr("Matrix form"), parent);
        QFont font = header->font();
        font.setBold(true);
        header->setFont(font);
        layout->addWidget(header);
        layout->addWidget(createLatexDisplayWidget(stateSpace.matrixForm, parent));
    }

    if (!stateSpace.outputs.isEmpty()) {
        const QString outputSummary =
            stateSpace.outputLabels.isEmpty() ? stateSpace.outputs.join(QStringLiteral(", "))
                                              : stateSpace.outputLabels.join(QStringLiteral(", "));
        addSection(QObject::tr("Outputs"), {outputSummary});
    }
    addSection(QObject::tr("Output equations"), stateSpace.outputEquations);

    if (!stateSpace.outputMatrixForm.isEmpty()) {
        auto* header = new QLabel(QObject::tr("Output matrix form"), parent);
        QFont font = header->font();
        font.setBold(true);
        header->setFont(font);
        layout->addWidget(header);
        layout->addWidget(createLatexDisplayWidget(stateSpace.outputMatrixForm, parent));
    }

    layout->addStretch();
}

}  // namespace lg

AnalyzeWindow::AnalyzeWindow(GraphScene* scene, GraphView* view, QWidget* parent)
    : QWidget(parent), m_scene(scene), m_view(view) {
    auto* normalTreeGroup = new QGroupBox(tr("Normal Tree"), this);
    auto* findBtn = new QPushButton(tr("Find All Normal Trees"), normalTreeGroup);
    auto* selectBtn = new QPushButton(tr("Select Manually"), normalTreeGroup);
    auto* clearBtn = new QPushButton(tr("Clear Highlight"), normalTreeGroup);
    connect(findBtn, &QPushButton::clicked, this, &AnalyzeWindow::runFindNormalTree);
    connect(selectBtn, &QPushButton::clicked, this, &AnalyzeWindow::runSelectNormalTree);
    connect(clearBtn, &QPushButton::clicked, this, &AnalyzeWindow::runClearNormalTree);

    auto* normalTreeButtons = new QHBoxLayout;
    normalTreeButtons->addWidget(findBtn);
    normalTreeButtons->addWidget(selectBtn);
    normalTreeButtons->addWidget(clearBtn);

    m_normalTreeStatus = new QLabel(normalTreeGroup);
    m_normalTreeStatus->setWordWrap(true);

    m_manualTreePanel = new QWidget(normalTreeGroup);
    m_manualTreeStatus = new QLabel(m_manualTreePanel);
    m_manualTreeStatus->setWordWrap(true);
    m_manualApplyBtn = new QPushButton(tr("Apply Selection"), m_manualTreePanel);
    auto* manualCancelBtn = new QPushButton(tr("Cancel"), m_manualTreePanel);
    connect(m_manualApplyBtn, &QPushButton::clicked, this, [this]() {
        m_scene->acceptManualNormalTreeSelection();
        m_view->setToolMode(GraphScene::Mode::Select);
        refreshNormalTreeSection();
        if (m_refreshCallback) {
            m_refreshCallback();
        }
    });
    connect(manualCancelBtn, &QPushButton::clicked, this, [this]() {
        m_scene->cancelManualNormalTreeSelection();
        m_view->setToolMode(GraphScene::Mode::Select);
        refreshNormalTreeSection();
        if (m_refreshCallback) {
            m_refreshCallback();
        }
    });
    auto* manualButtons = new QHBoxLayout;
    manualButtons->addWidget(m_manualApplyBtn);
    manualButtons->addWidget(manualCancelBtn);
    auto* manualLayout = new QVBoxLayout(m_manualTreePanel);
    manualLayout->setContentsMargins(0, 0, 0, 0);
    manualLayout->addWidget(m_manualTreeStatus);
    manualLayout->addLayout(manualButtons);

    auto* normalTreeLayout = new QVBoxLayout(normalTreeGroup);
    normalTreeLayout->addLayout(normalTreeButtons);
    normalTreeLayout->addWidget(m_normalTreeStatus);
    normalTreeLayout->addWidget(m_manualTreePanel);

    auto* validTreesGroup = new QGroupBox(tr("Valid Normal Trees"), this);
    m_validTreesList = new QListWidget(validTreesGroup);
    m_validTreesList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_validTreesList, &QListWidget::itemDoubleClicked, this, &AnalyzeWindow::runUseValidTree);
    connect(m_validTreesList, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        const int row = m_validTreesList->currentRow();
        if (row < 0) {
            return;
        }
        m_scene->showDiscoveredNormalTree(row);
        refreshNormalTreeSection();
        refreshValidTreesList();
        if (m_refreshCallback) {
            m_refreshCallback();
        }
    });

    auto* validTreesLayout = new QVBoxLayout(validTreesGroup);
    validTreesLayout->addWidget(m_validTreesList);

    auto* savedTreesGroup = new QGroupBox(tr("Saved Normal Trees"), this);
    m_savedTreesList = new QListWidget(savedTreesGroup);
    m_savedTreesList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_savedTreesList, &QListWidget::itemDoubleClicked, this, &AnalyzeWindow::runUseSavedTree);
    connect(m_savedTreesList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_removeTreeBtn->setEnabled(m_savedTreesList->currentRow() >= 0);
    });

    m_saveTreeBtn = new QPushButton(tr("Save Current"), savedTreesGroup);
    m_removeTreeBtn = new QPushButton(tr("Remove"), savedTreesGroup);
    connect(m_saveTreeBtn, &QPushButton::clicked, this, &AnalyzeWindow::runSaveCurrentTree);
    connect(m_removeTreeBtn, &QPushButton::clicked, this, &AnalyzeWindow::runRemoveSavedTree);

    auto* savedTreeButtons = new QHBoxLayout;
    savedTreeButtons->addWidget(m_saveTreeBtn);
    savedTreeButtons->addWidget(m_removeTreeBtn);

    auto* savedTreesLayout = new QVBoxLayout(savedTreesGroup);
    savedTreesLayout->addWidget(m_savedTreesList);
    savedTreesLayout->addLayout(savedTreeButtons);

    auto* stateSpaceGroup = new QGroupBox(tr("State Space"), this);
    auto* computeBtn = new QPushButton(tr("Compute State Space"), stateSpaceGroup);
    connect(computeBtn, &QPushButton::clicked, this, &AnalyzeWindow::runComputeStateSpace);
    m_exportMatlabBtn = new QPushButton(tr("Export %1 Script").arg(app::matlabRegistered()), stateSpaceGroup);
    m_exportMatlabBtn->setEnabled(false);
    connect(m_exportMatlabBtn, &QPushButton::clicked, this, [this]() {
        lg::exportStateSpaceMatlabScript(this, m_scene->lastStateSpaceResult());
    });
    auto* stateSpaceButtons = new QVBoxLayout;
    stateSpaceButtons->addWidget(computeBtn);
    stateSpaceButtons->addWidget(m_exportMatlabBtn);

    auto* outputsLabel = new QLabel(tr("Output variables (C and D matrices):"), stateSpaceGroup);
    outputsLabel->setWordWrap(true);
    auto* selectAllOutputsBtn = new QPushButton(tr("Select All"), stateSpaceGroup);
    auto* clearAllOutputsBtn = new QPushButton(tr("Clear All"), stateSpaceGroup);
    connect(selectAllOutputsBtn, &QPushButton::clicked, this,
            [this]() { setAllOutputVariablesChecked(true); });
    connect(clearAllOutputsBtn, &QPushButton::clicked, this,
            [this]() { setAllOutputVariablesChecked(false); });
    auto* outputHeader = new QHBoxLayout;
    outputHeader->addWidget(outputsLabel, 1);
    outputHeader->addWidget(selectAllOutputsBtn);
    outputHeader->addWidget(clearAllOutputsBtn);

    m_outputVariablesList = new QListWidget(stateSpaceGroup);
    m_outputVariablesList->setSelectionMode(QAbstractItemView::NoSelection);
    connect(m_outputVariablesList, &QListWidget::itemChanged, this,
            &AnalyzeWindow::syncOutputVariablesFromList);

    auto* stateSpaceHint =
        new QLabel(tr("Results appear in the State Space panel below."), stateSpaceGroup);
    stateSpaceHint->setWordWrap(true);
    auto* stateSpaceLayout = new QVBoxLayout(stateSpaceGroup);
    stateSpaceLayout->addLayout(stateSpaceButtons);
    stateSpaceLayout->addLayout(outputHeader);
    stateSpaceLayout->addWidget(m_outputVariablesList);
    stateSpaceLayout->addWidget(stateSpaceHint);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(normalTreeGroup);
    layout->addWidget(validTreesGroup);
    layout->addWidget(savedTreesGroup);
    layout->addWidget(stateSpaceGroup);
    layout->addStretch();

    connect(m_scene, &GraphScene::graphChanged, this, &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::graphChanged, this, &AnalyzeWindow::refreshValidTreesList);
    connect(m_scene, &GraphScene::graphChanged, this, &AnalyzeWindow::refreshSavedTreesList);
    connect(m_scene, &GraphScene::graphChanged, this, &AnalyzeWindow::refreshOutputVariablesList);
    connect(m_scene, &GraphScene::graphChanged, this, &AnalyzeWindow::refreshExportMatlabButton);
    connect(m_scene, &GraphScene::normalTreeHighlightChanged, this, &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::normalTreeHighlightChanged, this, &AnalyzeWindow::refreshSavedTreesList);
    connect(m_scene, &GraphScene::discoveredNormalTreesChanged, this, &AnalyzeWindow::refreshValidTreesList);
    connect(m_scene, &GraphScene::discoveredNormalTreesChanged, this, &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::manualNormalTreeValidationChanged, this,
            &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::manualNormalTreeAccepted, this, &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::manualNormalTreeRejected, this, &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::modeChanged, this, &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::savedNormalTreesChanged, this, &AnalyzeWindow::refreshSavedTreesList);

    refreshNormalTreeSection();
    refreshValidTreesList();
    refreshSavedTreesList();
    refreshOutputVariablesList();
    refreshExportMatlabButton();
}

void AnalyzeWindow::refreshExportMatlabButton() {
    if (!m_exportMatlabBtn) {
        return;
    }
    const lg::StateSpaceResult& result = m_scene->lastStateSpaceResult();
    m_exportMatlabBtn->setEnabled(result.status == lg::StateSpaceResult::Status::Ok
                                  && !result.matrices.A.empty());
}

void AnalyzeWindow::refreshOutputVariablesList() {
    if (!m_outputVariablesList) {
        return;
    }
    const QSignalBlocker blocker(m_outputVariablesList);
    const std::vector<lg::GraphOutputVariable> choices = m_scene->availableOutputVariables();
    QStringList selected = m_scene->outputVariables();
    QStringList validSelected;
    validSelected.reserve(selected.size());
    for (const QString& symbol : selected) {
        const bool stillAvailable = std::any_of(
            choices.begin(), choices.end(),
            [&](const lg::GraphOutputVariable& choice) { return choice.symbol == symbol; });
        if (stillAvailable) {
            validSelected.push_back(symbol);
        }
    }
    if (validSelected != selected) {
        m_scene->setOutputVariables(validSelected);
        selected = validSelected;
    }
    m_outputVariablesList->clear();
    for (const lg::GraphOutputVariable& choice : choices) {
        auto* item = new QListWidgetItem(choice.label, m_outputVariablesList);
        item->setData(Qt::UserRole, choice.symbol);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selected.contains(choice.symbol) ? Qt::Checked : Qt::Unchecked);
    }
}

void AnalyzeWindow::syncOutputVariablesFromList() {
    if (!m_outputVariablesList) {
        return;
    }
    QStringList selected;
    for (int row = 0; row < m_outputVariablesList->count(); ++row) {
        QListWidgetItem* item = m_outputVariablesList->item(row);
        if (item && item->checkState() == Qt::Checked) {
            selected.push_back(item->data(Qt::UserRole).toString());
        }
    }
    m_scene->setOutputVariables(selected);
}

void AnalyzeWindow::setAllOutputVariablesChecked(bool checked) {
    if (!m_outputVariablesList) {
        return;
    }
    const QSignalBlocker blocker(m_outputVariablesList);
    const Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;
    for (int row = 0; row < m_outputVariablesList->count(); ++row) {
        if (QListWidgetItem* item = m_outputVariablesList->item(row)) {
            item->setCheckState(state);
        }
    }
    syncOutputVariablesFromList();
}

void AnalyzeWindow::refreshValidTreesList() {
    const QSignalBlocker blocker(m_validTreesList);
    const int previousRow = m_validTreesList->currentRow();
    m_validTreesList->clear();
    const std::vector<lg::NormalTreeResult>& trees = m_scene->discoveredNormalTrees();
    for (int i = 0; i < static_cast<int>(trees.size()); ++i) {
        m_validTreesList->addItem(m_scene->discoveredNormalTreeListLabel(i));
    }
    if (previousRow >= 0 && previousRow < m_validTreesList->count()) {
        m_validTreesList->setCurrentRow(previousRow);
    } else if (m_scene->discoveredNormalTreeIndex() >= 0 &&
               m_scene->discoveredNormalTreeIndex() < m_validTreesList->count()) {
        m_validTreesList->setCurrentRow(m_scene->discoveredNormalTreeIndex());
    }
}

void AnalyzeWindow::refreshSavedTreesList() {
    const int previousRow = m_savedTreesList->currentRow();
    m_savedTreesList->clear();
    const std::vector<GraphScene::SavedNormalTree>& trees = m_scene->savedNormalTrees();
    for (int i = 0; i < static_cast<int>(trees.size()); ++i) {
        m_savedTreesList->addItem(m_scene->savedNormalTreeListLabel(i));
    }
    if (previousRow >= 0 && previousRow < m_savedTreesList->count()) {
        m_savedTreesList->setCurrentRow(previousRow);
    } else if (m_scene->activeSavedNormalTreeIndex() >= 0) {
        m_savedTreesList->setCurrentRow(m_scene->activeSavedNormalTreeIndex());
    }

    const bool hasActiveTree =
        m_scene->normalTreeHighlightActive() &&
        m_scene->lastNormalTreeResult().status == lg::NormalTreeResult::Status::Ok;
    m_saveTreeBtn->setEnabled(hasActiveTree);

    const bool hasSelection = m_savedTreesList->currentRow() >= 0;
    m_removeTreeBtn->setEnabled(hasSelection);
}

void AnalyzeWindow::refreshNormalTreeSection() {
    const bool manualMode = m_scene->mode() == GraphScene::Mode::SelectNormalTree;
    m_manualTreePanel->setVisible(manualMode);

    if (manualMode) {
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
        m_manualTreeStatus->setText(
            tr("Editing on canvas — %1 branches in tree. %2")
                .arg(inTreeCount)
                .arg(valid ? tr("Selection is valid.")
                           : (validation.message.isEmpty() ? tr("Selection is invalid.")
                                                           : validation.message)));
        m_manualApplyBtn->setEnabled(valid);
        m_normalTreeStatus->setText(tr("Click branches on the graph to toggle tree (green) / link (gray)."));
        return;
    }

    if (!m_scene->normalTreeHighlightActive()) {
        m_normalTreeStatus->setText(tr("No normal tree selected. Use Find All or Select Manually."));
        return;
    }

    const lg::NormalTreeResult& normalTree = m_scene->lastNormalTreeResult();
    const QString source = m_scene->normalTreeIsManual() ? tr("Manual selection") : tr("Auto-detected");
    const int savedIndex = m_scene->activeSavedNormalTreeIndex();
    const QString savedName =
        savedIndex >= 0 ? m_scene->savedNormalTrees()[static_cast<size_t>(savedIndex)].name : QString();

    QStringList stateVarText;
    for (const lg::NormalTreeResult::StateVariable& state : normalTree.stateVariables) {
        stateVarText.push_back(state.symbol);
    }
    const QString stateVarSummary =
        stateVarText.isEmpty() ? tr("none") : stateVarText.join(QStringLiteral(", "));

    const QString sourceLabel =
        savedName.isEmpty() ? source : tr("%1 (saved: %2)").arg(source, savedName);

    m_normalTreeStatus->setText(
        tr("%1 — %2 tree branches, system order %3.\nState variables: %4")
            .arg(sourceLabel)
            .arg(normalTree.treeBranches.size())
            .arg(normalTree.stateVariables.size())
            .arg(stateVarSummary));
}

void AnalyzeWindow::runFindNormalTree() {
    const lg::NormalTreeEnumerationResult all = m_scene->findAllNormalTrees();
    if (!all.trees.empty()) {
        refreshNormalTreeSection();
        refreshValidTreesList();
        refreshSavedTreesList();
        if (m_refreshCallback) {
            m_refreshCallback();
        }
        return;
    }
    refreshValidTreesList();
    QMessageBox::warning(this, tr("Normal Tree"),
                         all.message.isEmpty() ? tr("Could not find a valid normal tree.")
                                               : all.message);
}

void AnalyzeWindow::runUseValidTree() {
    const int row = m_validTreesList->currentRow();
    if (row < 0) {
        return;
    }
    if (!m_scene->showDiscoveredNormalTree(row)) {
        QMessageBox::warning(this, tr("Normal Tree"), tr("Could not apply the selected tree."));
        return;
    }
    refreshNormalTreeSection();
    refreshValidTreesList();
    refreshSavedTreesList();
    if (m_refreshCallback) {
        m_refreshCallback();
    }
}

void AnalyzeWindow::runSelectNormalTree() {
    m_scene->beginManualNormalTreeSelection();
    m_view->setToolMode(GraphScene::Mode::SelectNormalTree);
    refreshNormalTreeSection();
    if (m_refreshCallback) {
        m_refreshCallback();
    }
}

void AnalyzeWindow::runClearNormalTree() {
    m_scene->clearNormalTreeHighlight();
    refreshNormalTreeSection();
    refreshValidTreesList();
    refreshSavedTreesList();
    if (m_refreshCallback) {
        m_refreshCallback();
    }
}

void AnalyzeWindow::runSaveCurrentTree() {
    const int nextIndex = static_cast<int>(m_scene->savedNormalTrees().size()) + 1;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Save Normal Tree"), tr("Tree name:"), QLineEdit::Normal,
        tr("Tree %1").arg(nextIndex), &ok);
    if (!ok) {
        return;
    }
    if (!m_scene->addSavedNormalTree(name)) {
        QMessageBox::warning(this, tr("Save Normal Tree"),
                             tr("Find or select a valid normal tree first."));
        return;
    }
    refreshNormalTreeSection();
    refreshSavedTreesList();
    if (m_refreshCallback) {
        m_refreshCallback();
    }
}

void AnalyzeWindow::runRemoveSavedTree() {
    const int row = m_savedTreesList->currentRow();
    if (row < 0) {
        return;
    }
    if (!m_scene->removeSavedNormalTree(row)) {
        return;
    }
    refreshNormalTreeSection();
    refreshSavedTreesList();
    if (m_refreshCallback) {
        m_refreshCallback();
    }
}

void AnalyzeWindow::runUseSavedTree() {
    const int row = m_savedTreesList->currentRow();
    if (row < 0) {
        return;
    }
    if (!m_scene->applySavedNormalTree(row)) {
        QMessageBox::warning(this, tr("Normal Tree"),
                             tr("Could not apply the selected saved normal tree."));
        return;
    }
    refreshNormalTreeSection();
    refreshSavedTreesList();
    if (m_refreshCallback) {
        m_refreshCallback();
    }
}

void AnalyzeWindow::runComputeStateSpace() {
    const lg::StateSpaceResult result = m_scene->computeStateSpaceRep();
    if (result.status == lg::StateSpaceResult::Status::Ok) {
        refreshExportMatlabButton();
        emit stateSpaceComputed();
        if (m_refreshCallback) {
            m_refreshCallback();
        }
        return;
    }
    QMessageBox::warning(this, tr("State Space"),
                         result.message.isEmpty() ? tr("State-space computation failed.")
                                                  : result.message);
}
