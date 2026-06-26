#include "state_space_context.h"

#include <symengine/add.h>
#include <symengine/subs.h>

#include <algorithm>

namespace lg {

StateSpaceContext::StateSpaceContext(const NormalTreeResult& t, const std::vector<NodeItem*>& n,
                                     const std::vector<BranchItem*>& b,
                                     const std::vector<TwoPortItem*>& tp)
    : tree(t), nodes(n), branches(b), twoPorts(tp) {}

bool StateSpaceContext::initStates() {
    using SymEngine::expand;
    using SymEngine::sub;

    if (tree.status != NormalTreeResult::Status::Ok) {
        ss::ssLog(QStringLiteral("abort"), QStringLiteral("need normal tree"));
        result.status = StateSpaceResult::Status::NeedNormalTree;
        result.message = QStringLiteral("Find a valid normal tree first.");
        return false;
    }
    if (branches.empty()) {
        ss::ssLog(QStringLiteral("abort"), QStringLiteral("no branches"));
        result.status = StateSpaceResult::Status::GraphError;
        result.message = QStringLiteral("The graph has no branches.");
        return false;
    }

    treeSet = std::unordered_set<BranchItem*>(tree.treeBranches.begin(), tree.treeBranches.end());
    for (const NormalTreeResult::StateVariable& state : tree.stateVariables) {
        result.stateVariables.push_back(state.symbol);
    }

    computedStates.reserve(tree.stateVariables.size());
    for (const NormalTreeResult::StateVariable& treeState : tree.stateVariables) {
        BranchItem* match = nullptr;
        for (BranchItem* branch : branches) {
            if (!ss::isStateBranch(tree, branch, branches, twoPorts)) {
                continue;
            }
            const bool inTree = treeSet.count(branch) != 0;
            const QString branchSymbol =
                treeState.kind == NormalTreeResult::StateVariable::Kind::Across
                    ? branchAcrossSymbol(*branch)
                    : branchThroughSymbol(*branch);
            if (branchSymbol != treeState.symbol) {
                const QString storageSymbol = ss::storageStateSymbol(*branch, inTree);
                if (storageSymbol != treeState.symbol) {
                    continue;
                }
            }
            match = branch;
            break;
        }
        if (!match) {
            ss::ssLog(QStringLiteral("abort"),
                      QStringLiteral("unmapped state %1").arg(treeState.symbol));
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message = QStringLiteral("Could not map state variable %1 to a storage branch.")
                                 .arg(treeState.symbol);
            return false;
        }
        computedStates.push_back({treeState.symbol, match});
    }

    QStringList stateDesc;
    for (const ComputedState& state : computedStates) {
        const QString branchName = state.branch ? state.branch->name() : QStringLiteral("?");
        stateDesc.push_back(QStringLiteral("%1 (%2)").arg(state.symbol, branchName));
    }
    ss::ssLog(QStringLiteral("begin"),
              QStringLiteral("states=[%1] tree_branches=%2")
                  .arg(stateDesc.join(QStringLiteral(", ")))
                  .arg(tree.treeBranches.size()));
    return true;
}

void StateSpaceContext::addEquationText(QStringList& list, const QString& lhs,
                                        const ss::RCP<const ss::Basic>& rhs) {
    list.push_back(QStringLiteral("%1 = %2").arg(lhs, ss::exprText(rhs)));
}

void StateSpaceContext::recordReplacement(const QString& secondary,
                                        const ss::RCP<const ss::Basic>& expr, QStringList& list) {
    using SymEngine::expand;
    using SymEngine::sub;
    if (secondary.trimmed().isEmpty()) {
        return;
    }
    if (isValidVariableSymbol(secondary)) {
        constraintEquations.push_back(expand(sub(ss::symOf(secondary), expr)));
    }
    replacements[secondary] = expr;
    addEquationText(list, secondary, expr);
}

void StateSpaceContext::recordConstraint(const QString& secondary, const ss::RCP<const ss::Basic>& expr,
                                         bool overwrite) {
    using SymEngine::expand;
    using SymEngine::sub;
    if (secondary.trimmed().isEmpty()) {
        return;
    }
    if (!overwrite && replacements.count(secondary) != 0) {
        return;
    }
    if (isValidVariableSymbol(secondary)) {
        constraintEquations.push_back(expand(sub(ss::symOf(secondary), expr)));
    }
    replacements[secondary] = expr;
}

void StateSpaceContext::appendTimed(const QString& symbol) {
    if (symbol.isEmpty() || !isValidVariableSymbol(symbol)) {
        return;
    }
    if (std::find(timedSymbols.begin(), timedSymbols.end(), symbol) == timedSymbols.end()) {
        timedSymbols.push_back(symbol);
    }
}

bool StateSpaceContext::isPrimaryState(const QString& symbolName) const {
    return std::any_of(computedStates.begin(), computedStates.end(),
                       [&](const ComputedState& state) { return state.symbol == symbolName; });
}

}  // namespace lg
