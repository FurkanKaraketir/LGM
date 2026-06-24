#include "analyze_window.h"

#include "canvas.h"
#include "latex_widget.h"
#include "normal_tree.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace lg {

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
            label->setWordWrap(true);
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

    layout->addStretch();
}

}  // namespace lg

AnalyzeWindow::AnalyzeWindow(GraphScene* scene, GraphView* view, QWidget* parent)
    : QWidget(parent), m_scene(scene), m_view(view) {
    auto* normalTreeGroup = new QGroupBox(tr("Normal Tree"), this);
    auto* findBtn = new QPushButton(tr("Find Normal Tree"), normalTreeGroup);
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

    auto* stateSpaceGroup = new QGroupBox(tr("State Space"), this);
    auto* computeBtn = new QPushButton(tr("Compute State Space"), stateSpaceGroup);
    connect(computeBtn, &QPushButton::clicked, this, &AnalyzeWindow::runComputeStateSpace);
    auto* stateSpaceHint =
        new QLabel(tr("Results appear in the State Space panel below."), stateSpaceGroup);
    stateSpaceHint->setWordWrap(true);
    auto* stateSpaceLayout = new QVBoxLayout(stateSpaceGroup);
    stateSpaceLayout->addWidget(computeBtn);
    stateSpaceLayout->addWidget(stateSpaceHint);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(normalTreeGroup);
    layout->addWidget(stateSpaceGroup);
    layout->addStretch();

    connect(m_scene, &GraphScene::graphChanged, this, &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::normalTreeHighlightChanged, this, &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::manualNormalTreeValidationChanged, this,
            &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::manualNormalTreeAccepted, this, &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::manualNormalTreeRejected, this, &AnalyzeWindow::refreshNormalTreeSection);
    connect(m_scene, &GraphScene::modeChanged, this, &AnalyzeWindow::refreshNormalTreeSection);

    refreshNormalTreeSection();
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
        m_normalTreeStatus->setText(tr("No normal tree selected. Use Find or Select Manually."));
        return;
    }

    const lg::NormalTreeResult& normalTree = m_scene->lastNormalTreeResult();
    const QString source = m_scene->normalTreeIsManual() ? tr("Manual selection") : tr("Auto-detected");

    QStringList stateVarText;
    for (const lg::NormalTreeResult::StateVariable& state : normalTree.stateVariables) {
        stateVarText.push_back(state.symbol);
    }
    const QString stateVarSummary =
        stateVarText.isEmpty() ? tr("none") : stateVarText.join(QStringLiteral(", "));

    m_normalTreeStatus->setText(
        tr("%1 — %2 tree branches, system order %3.\nState variables: %4")
            .arg(source)
            .arg(normalTree.treeBranches.size())
            .arg(normalTree.stateVariables.size())
            .arg(stateVarSummary));
}

void AnalyzeWindow::runFindNormalTree() {
    const lg::NormalTreeResult result = m_scene->findNormalTree();
    if (result.status == lg::NormalTreeResult::Status::Ok) {
        refreshNormalTreeSection();
        if (m_refreshCallback) {
            m_refreshCallback();
        }
        return;
    }
    QMessageBox::warning(this, tr("Normal Tree"),
                         result.message.isEmpty() ? tr("Could not find a valid normal tree.")
                                                  : result.message);
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
    if (m_refreshCallback) {
        m_refreshCallback();
    }
}

void AnalyzeWindow::runComputeStateSpace() {
    const lg::StateSpaceResult result = m_scene->computeStateSpaceRep();
    if (result.status == lg::StateSpaceResult::Status::Ok) {
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
