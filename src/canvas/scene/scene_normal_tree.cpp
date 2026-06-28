#include "canvas.h"

void GraphScene::clearNormalTreeHighlight() {
    if (!m_normalTreeHighlightActive && m_mode != Mode::SelectNormalTree) {
        return;
    }
    const bool hadTree = m_normalTreeHighlightActive;
    m_normalTreeHighlightActive = false;
    m_normalTreeManual = false;
    m_lastNormalTreeResult = {};
    m_lastStateSpaceResult = {};
    m_manualNormalTreeValidation = {};
    const bool hadDiscovered = !m_discoveredNormalTrees.empty();
    m_discoveredNormalTrees.clear();
    m_discoveredNormalTreeIndex = -1;
    for (QGraphicsItem* item : items()) {
        if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branch->setNormalTreeRole(false, false);
        }
    }
    if (m_mode == Mode::SelectNormalTree) {
        m_manualNormalTreeBackup = {};
        m_mode = Mode::Select;
        setNormalTreePickingPassthrough(false);
        emit modeChanged(Mode::Select);
    }
    if (hadTree) {
        m_activeSavedNormalTreeIndex = -1;
        emit normalTreeHighlightChanged();
    }
    if (hadDiscovered) {
        emit discoveredNormalTreesChanged();
    }
}

void GraphScene::collectGraphItems(std::vector<NodeItem*>& nodes, std::vector<BranchItem*>& branches,
                                   std::vector<TwoPortItem*>& twoPorts) const {
    nodes.clear();
    branches.clear();
    twoPorts.clear();
    nodes.reserve(static_cast<size_t>(items().size()));
    branches.reserve(static_cast<size_t>(items().size()));
    twoPorts.reserve(static_cast<size_t>(items().size()));
    for (QGraphicsItem* item : items()) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            nodes.push_back(node);
        } else if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branches.push_back(branch);
        } else if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            twoPorts.push_back(twoPort);
        }
    }
}

void GraphScene::refreshManualNormalTreeValidation() {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    collectGraphItems(nodes, branches, twoPorts);

    std::vector<BranchItem*> treeBranches;
    treeBranches.reserve(branches.size());
    for (BranchItem* branch : branches) {
        if (branch && branch->normalTreeRoleKnown() && branch->inNormalTree()) {
            treeBranches.push_back(branch);
        }
    }

    m_manualNormalTreeValidation =
        lg::validateManualNormalTree(nodes, branches, twoPorts, treeBranches);
    if (m_manualNormalTreeValidation.status == lg::NormalTreeResult::Status::Ok) {
        m_lastNormalTreeResult = m_manualNormalTreeValidation;
        m_lastStateSpaceResult = {};
        m_normalTreeHighlightActive = true;
    }
    emit manualNormalTreeValidationChanged(m_manualNormalTreeValidation);
    emit normalTreeHighlightChanged();
}

void GraphScene::restoreManualNormalTreeBackup() {
    m_normalTreeHighlightActive = m_manualNormalTreeBackup.highlightActive;
    m_lastNormalTreeResult = m_manualNormalTreeBackup.result;
    m_lastStateSpaceResult = {};
    m_manualNormalTreeValidation = {};

    const std::unordered_set<BranchItem*> inTree(m_lastNormalTreeResult.treeBranches.begin(),
                                                 m_lastNormalTreeResult.treeBranches.end());
    for (QGraphicsItem* item : items()) {
        auto* branch = dynamic_cast<BranchItem*>(item);
        if (!branch) {
            continue;
        }
        if (!m_normalTreeHighlightActive) {
            branch->setNormalTreeRole(false, false);
            continue;
        }
        branch->setNormalTreeRole(inTree.count(branch) != 0, true);
    }
    emit normalTreeHighlightChanged();
}

void GraphScene::leaveManualNormalTreeMode(bool accept) {
    if (m_mode != Mode::SelectNormalTree) {
        return;
    }

    refreshManualNormalTreeValidation();
    const lg::NormalTreeResult validation = m_manualNormalTreeValidation;

    // ponytail: leave manual mode before signals — manualNormalTreeAccepted calls
    // setToolMode(Select), which would re-enter here with cleared validation.
    m_mode = Mode::Select;
    setNormalTreePickingPassthrough(false);

    if (accept && validation.status == lg::NormalTreeResult::Status::Ok) {
        m_normalTreeManual = true;
        m_manualNormalTreeBackup = {};
        m_manualNormalTreeValidation = {};
        syncActiveSavedNormalTreeIndex();
        emit manualNormalTreeAccepted(m_lastNormalTreeResult);
    } else if (accept) {
        const QString message =
            validation.message.isEmpty()
                ? QStringLiteral("The manual normal tree selection is not valid.")
                : validation.message;
        restoreManualNormalTreeBackup();
        m_manualNormalTreeValidation = {};
        emit manualNormalTreeRejected(message);
    } else {
        restoreManualNormalTreeBackup();
        m_manualNormalTreeValidation = {};
    }

    emit modeChanged(Mode::Select);
}

void GraphScene::acceptManualNormalTreeSelection() {
    leaveManualNormalTreeMode(true);
}

void GraphScene::cancelManualNormalTreeSelection() {
    leaveManualNormalTreeMode(false);
}

void GraphScene::setNormalTreePickingPassthrough(bool enabled) {
    // ponytail: coupler shape sits above branches (z=2); pass clicks through in manual tree mode
    const Qt::MouseButtons buttons = enabled ? Qt::NoButton : Qt::LeftButton;
    for (QGraphicsItem* item : items()) {
        if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            twoPort->setAcceptedMouseButtons(buttons);
            continue;
        }
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            if (node->twoPort()) {
                node->setAcceptedMouseButtons(buttons);
            }
        }
    }
}

void GraphScene::setManualNormalTreeBranchRole(BranchItem* branch, bool inTree) {
    if (!branch || m_mode != Mode::SelectNormalTree) {
        return;
    }
    if (branch->isActive() && branch->branchType() == BranchType::A && !inTree) {
        return;
    }

    if (TwoPortItem* twoPort = twoPortFor(branch)) {
        if (isInternalTwoPortBranch(twoPort, branch)) {
            if (twoPort->kind() == TwoPortKind::Gyrator) {
                if (BranchItem* left = twoPort->leftBranch()) {
                    left->setNormalTreeRole(inTree, true);
                }
                if (BranchItem* right = twoPort->rightBranch()) {
                    right->setNormalTreeRole(inTree, true);
                }
                refreshManualNormalTreeValidation();
                return;
            }
            BranchItem* left = twoPort->leftBranch();
            BranchItem* right = twoPort->rightBranch();
            if (inTree) {
                if (branch == right) {
                    if (left) {
                        left->setNormalTreeRole(false, true);
                    }
                    branch->setNormalTreeRole(true, true);
                } else {
                    if (right) {
                        right->setNormalTreeRole(false, true);
                    }
                    branch->setNormalTreeRole(true, true);
                }
            } else if (branch->inNormalTree()) {
                branch->setNormalTreeRole(false, true);
            }
            refreshManualNormalTreeValidation();
            return;
        }
    }

    branch->setNormalTreeRole(inTree, true);
    refreshManualNormalTreeValidation();
}

void GraphScene::toggleManualNormalTreeBranch(BranchItem* branch) {
    if (!branch || m_mode != Mode::SelectNormalTree) {
        return;
    }
    setManualNormalTreeBranchRole(branch, !branch->inNormalTree());
}

void GraphScene::beginManualNormalTreeSelection() {
    m_manualNormalTreeBackup.highlightActive = m_normalTreeHighlightActive;
    m_manualNormalTreeBackup.result = m_lastNormalTreeResult;

    const lg::NormalTreeResult seed = findNormalTree();
    if (seed.status != lg::NormalTreeResult::Status::Ok) {
        std::vector<NodeItem*> nodes;
        std::vector<BranchItem*> branches;
        std::vector<TwoPortItem*> twoPorts;
        collectGraphItems(nodes, branches, twoPorts);
        m_normalTreeHighlightActive = true;
        for (BranchItem* branch : branches) {
            if (!branch) {
                continue;
            }
            const bool inTree = branch->isActive() && branch->branchType() == BranchType::A;
            branch->setNormalTreeRole(inTree, true);
        }
        m_lastNormalTreeResult = {};
        m_lastStateSpaceResult = {};
    }

    if (m_mode == Mode::AddBranch) {
        clearBranchPending();
    }
    m_mode = Mode::SelectNormalTree;
    emit modeChanged(Mode::SelectNormalTree);
    setNormalTreePickingPassthrough(true);
    refreshManualNormalTreeValidation();
}

QString GraphScene::takeLoadWarning() {
    const QString warning = m_loadWarning;
    m_loadWarning.clear();
    return warning;
}

void GraphScene::applyNormalTreeHighlight(const lg::NormalTreeResult& result) {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    collectGraphItems(nodes, branches, twoPorts);

    m_normalTreeHighlightActive = true;
    m_lastNormalTreeResult = result;
    m_lastStateSpaceResult = {};
    for (BranchItem* branch : branches) {
        if (!branch) {
            continue;
        }
        const bool inTree =
            std::find(result.treeBranches.begin(), result.treeBranches.end(), branch) !=
            result.treeBranches.end();
        branch->setNormalTreeRole(inTree, true);
    }
    syncActiveSavedNormalTreeIndex();
    emit normalTreeHighlightChanged();
}

lg::NormalTreeEnumerationResult GraphScene::findAllNormalTrees() {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    collectGraphItems(nodes, branches, twoPorts);

    for (QGraphicsItem* item : items()) {
        if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branch->setNormalTreeRole(false, false);
        }
    }
    m_normalTreeHighlightActive = false;
    m_normalTreeManual = false;
    m_lastNormalTreeResult = {};
    m_lastStateSpaceResult = {};
    m_discoveredNormalTrees.clear();
    m_discoveredNormalTreeIndex = -1;

    const lg::NormalTreeEnumerationResult enumeration =
        lg::enumerateNormalTrees(nodes, branches, twoPorts);
    m_discoveredNormalTrees = enumeration.trees;
    emit discoveredNormalTreesChanged();

    if (m_discoveredNormalTrees.empty()) {
        lg::NormalTreeEnumerationResult failure = enumeration;
        failure.trees.clear();
        return failure;
    }

    m_discoveredNormalTreeIndex = 0;
    m_normalTreeManual = false;
    applyNormalTreeHighlight(m_discoveredNormalTrees.front());
    return enumeration;
}

lg::NormalTreeResult GraphScene::findNormalTree() {
    const lg::NormalTreeEnumerationResult all = findAllNormalTrees();
    lg::NormalTreeResult result;
    if (all.trees.empty()) {
        result.status = all.status;
        result.message = all.message;
        return result;
    }
    return all.trees.front();
}

bool GraphScene::showDiscoveredNormalTree(int index) {
    if (index < 0 || index >= static_cast<int>(m_discoveredNormalTrees.size())) {
        return false;
    }
    m_normalTreeManual = false;
    m_discoveredNormalTreeIndex = index;
    applyNormalTreeHighlight(m_discoveredNormalTrees[static_cast<size_t>(index)]);
    return true;
}

QString GraphScene::discoveredNormalTreeListLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(m_discoveredNormalTrees.size())) {
        return {};
    }
    const lg::NormalTreeResult& tree = m_discoveredNormalTrees[static_cast<size_t>(index)];
    QStringList stateVarText;
    stateVarText.reserve(static_cast<int>(tree.stateVariables.size()));
    for (const lg::NormalTreeResult::StateVariable& state : tree.stateVariables) {
        stateVarText.push_back(state.symbol);
    }
    const QString stateVarSummary =
        stateVarText.isEmpty() ? tr("none") : stateVarText.join(QStringLiteral(", "));
    const QString activeMark =
        index == m_discoveredNormalTreeIndex ? QStringLiteral(" *") : QString();
    return tr("Tree %1 — order %2: %3%4")
        .arg(index + 1)
        .arg(tree.stateVariables.size())
        .arg(stateVarSummary)
        .arg(activeMark);
}

lg::NormalTreeResult GraphScene::commitNormalTreeSelection(
    const std::vector<BranchItem*>& treeBranches) {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    collectGraphItems(nodes, branches, twoPorts);

    lg::NormalTreeResult result =
        lg::validateManualNormalTree(nodes, branches, twoPorts, treeBranches);
    if (result.status != lg::NormalTreeResult::Status::Ok) {
        return result;
    }

    const std::unordered_set<BranchItem*> inTree(treeBranches.begin(), treeBranches.end());
    for (BranchItem* branch : branches) {
        if (branch) {
            branch->setNormalTreeRole(inTree.count(branch) != 0, true);
        }
    }
    m_normalTreeHighlightActive = true;
    m_lastNormalTreeResult = result;
    m_lastStateSpaceResult = {};
    syncActiveSavedNormalTreeIndex();
    emit normalTreeHighlightChanged();
    return result;
}

BranchItem* GraphScene::branchBySerialId(int serialId) const {
    if (serialId <= 0) {
        return nullptr;
    }
    for (QGraphicsItem* item : items()) {
        if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            if (branch->serialId() == serialId) {
                return branch;
            }
        }
    }
    return nullptr;
}

std::vector<int> GraphScene::currentTreeBranchSerialIds() const {
    std::vector<int> ids;
    if (!m_normalTreeHighlightActive ||
        m_lastNormalTreeResult.status != lg::NormalTreeResult::Status::Ok) {
        return ids;
    }
    ids.reserve(m_lastNormalTreeResult.treeBranches.size());
    for (BranchItem* branch : m_lastNormalTreeResult.treeBranches) {
        if (branch && branch->serialId() > 0) {
            ids.push_back(branch->serialId());
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

void GraphScene::syncActiveSavedNormalTreeIndex() {
    const std::vector<int> currentIds = currentTreeBranchSerialIds();
    if (currentIds.empty()) {
        m_activeSavedNormalTreeIndex = -1;
        return;
    }
    for (int i = 0; i < static_cast<int>(m_savedNormalTrees.size()); ++i) {
        std::vector<int> savedIds = m_savedNormalTrees[static_cast<size_t>(i)].treeBranchSerialIds;
        std::sort(savedIds.begin(), savedIds.end());
        if (savedIds == currentIds) {
            m_activeSavedNormalTreeIndex = i;
            return;
        }
    }
    m_activeSavedNormalTreeIndex = -1;
}

QString GraphScene::savedNormalTreeListLabel(int index) const {
    if (index < 0 || index >= static_cast<int>(m_savedNormalTrees.size())) {
        return {};
    }
    const SavedNormalTree& saved = m_savedNormalTrees[static_cast<size_t>(index)];
    std::vector<BranchItem*> treeBranches;
    treeBranches.reserve(saved.treeBranchSerialIds.size());
    for (int serialId : saved.treeBranchSerialIds) {
        if (BranchItem* branch = branchBySerialId(serialId)) {
            treeBranches.push_back(branch);
        }
    }

    QString detail;
    if (treeBranches.size() != saved.treeBranchSerialIds.size()) {
        detail = tr("missing branches");
    } else {
        std::vector<NodeItem*> nodes;
        std::vector<BranchItem*> branches;
        std::vector<TwoPortItem*> twoPorts;
        collectGraphItems(nodes, branches, twoPorts);
        const lg::NormalTreeResult preview =
            lg::validateManualNormalTree(nodes, branches, twoPorts, treeBranches);
        if (preview.status == lg::NormalTreeResult::Status::Ok) {
            detail = tr("order %1, %2 branches")
                         .arg(preview.stateVariables.size())
                         .arg(preview.treeBranches.size());
        } else {
            detail = preview.message.isEmpty() ? tr("invalid") : preview.message;
        }
    }

    const QString activeMark = index == m_activeSavedNormalTreeIndex ? QStringLiteral(" *") : QString();
    return tr("%1 — %2%3").arg(saved.name, detail, activeMark);
}

bool GraphScene::addSavedNormalTree(const QString& name) {
    if (!m_normalTreeHighlightActive ||
        m_lastNormalTreeResult.status != lg::NormalTreeResult::Status::Ok) {
        return false;
    }
    const std::vector<int> ids = currentTreeBranchSerialIds();
    if (ids.empty()) {
        return false;
    }

    const std::vector<SavedNormalTree> before = m_savedNormalTrees;
    const int beforeActive = m_activeSavedNormalTreeIndex;

    SavedNormalTree saved;
    saved.name = name.trimmed().isEmpty() ? tr("Tree %1").arg(m_savedNormalTrees.size() + 1) : name.trimmed();
    saved.treeBranchSerialIds = ids;

    std::vector<SavedNormalTree> after = m_savedNormalTrees;
    after.push_back(saved);
    const int afterActive = static_cast<int>(after.size()) - 1;

    pushSavedNormalTreesUndo(before, beforeActive, after, afterActive);
    syncActiveSavedNormalTreeIndex();
    return true;
}

bool GraphScene::removeSavedNormalTree(int index) {
    if (index < 0 || index >= static_cast<int>(m_savedNormalTrees.size())) {
        return false;
    }

    const std::vector<SavedNormalTree> before = m_savedNormalTrees;
    const int beforeActive = m_activeSavedNormalTreeIndex;

    std::vector<SavedNormalTree> after = m_savedNormalTrees;
    after.erase(after.begin() + index);

    int afterActive = beforeActive;
    if (beforeActive == index) {
        afterActive = -1;
    } else if (beforeActive > index) {
        --afterActive;
    }

    pushSavedNormalTreesUndo(before, beforeActive, after, afterActive);
    syncActiveSavedNormalTreeIndex();
    return true;
}

bool GraphScene::applySavedNormalTree(int index) {
    if (index < 0 || index >= static_cast<int>(m_savedNormalTrees.size())) {
        return false;
    }
    const SavedNormalTree& saved = m_savedNormalTrees[static_cast<size_t>(index)];

    std::vector<BranchItem*> treeBranches;
    treeBranches.reserve(saved.treeBranchSerialIds.size());
    for (int serialId : saved.treeBranchSerialIds) {
        if (BranchItem* branch = branchBySerialId(serialId)) {
            treeBranches.push_back(branch);
        }
    }
    if (treeBranches.size() != saved.treeBranchSerialIds.size()) {
        return false;
    }

    const lg::NormalTreeResult result = commitNormalTreeSelection(treeBranches);
    if (result.status != lg::NormalTreeResult::Status::Ok) {
        return false;
    }
    m_normalTreeManual = true;
    m_activeSavedNormalTreeIndex = index;
    emit savedNormalTreesChanged();
    return true;
}

lg::StateSpaceResult GraphScene::computeStateSpaceRep() {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    std::vector<TwoPortItem*> twoPorts;
    for (QGraphicsItem* item : items()) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            nodes.push_back(node);
        } else if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branches.push_back(branch);
        } else if (auto* twoPort = dynamic_cast<TwoPortItem*>(item)) {
            twoPorts.push_back(twoPort);
        }
    }

    if (!m_normalTreeHighlightActive ||
        m_lastNormalTreeResult.status != lg::NormalTreeResult::Status::Ok) {
        lg::StateSpaceResult result;
        result.status = lg::StateSpaceResult::Status::NeedNormalTree;
        result.message = QStringLiteral("Find a valid normal tree first.");
        m_lastStateSpaceResult = result;
        return result;
    }

    m_lastStateSpaceResult =
        lg::computeStateSpace(m_lastNormalTreeResult, nodes, branches, twoPorts, m_outputVariables);
    return m_lastStateSpaceResult;
}

std::vector<lg::GraphOutputVariable> GraphScene::availableOutputVariables() const {
    std::vector<NodeItem*> nodes;
    std::vector<BranchItem*> branches;
    for (QGraphicsItem* item : items()) {
        if (auto* node = dynamic_cast<NodeItem*>(item)) {
            nodes.push_back(node);
        } else if (auto* branch = dynamic_cast<BranchItem*>(item)) {
            branches.push_back(branch);
        }
    }
    return lg::collectOutputVariableChoices(nodes, branches);
}

