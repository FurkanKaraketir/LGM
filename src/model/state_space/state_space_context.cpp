#include "state_space_context.h"

#include <algorithm>

namespace lg {

StateSpaceContext::StateSpaceContext(const NormalTreeResult& t, const std::vector<NodeItem*>& n,
                                     const std::vector<BranchItem*>& b,
                                     const std::vector<TwoPortItem*>& tp)
    : tree(t), nodes(n), branches(b), twoPorts(tp) {}

bool StateSpaceContext::initStates() {
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
    computedStates.reserve(tree.stateVariables.size());
    for (const NormalTreeResult::StateVariable& state : tree.stateVariables) {
        if (!state.branch ||
            std::find(branches.begin(), branches.end(), state.branch) == branches.end()) {
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message = QStringLiteral("Could not map state variable %1 to a storage branch.")
                                 .arg(state.symbol);
            return false;
        }
        result.stateVariables.push_back(state.symbol);
        computedStates.push_back({state.symbol, state.branch});
    }
    ss::ssLog(QStringLiteral("begin"),
              QStringLiteral("states=[%1] tree_branches=%2")
                  .arg(result.stateVariables.join(QStringLiteral(", ")))
                  .arg(tree.treeBranches.size()));
    return true;
}

void StateSpaceContext::addEquationText(QStringList& list, const QString& lhs,
                                        const ss::RCP<const ss::Basic>& rhs) {
    list.push_back(QStringLiteral("%1 = %2").arg(lhs, ss::exprText(rhs)));
}

void StateSpaceContext::appendTimed(const QString& symbol) {
    if (symbol.isEmpty() || !isValidVariableSymbol(symbol)) {
        return;
    }
    if (std::find(timedSymbols.begin(), timedSymbols.end(), symbol) == timedSymbols.end()) {
        timedSymbols.push_back(symbol);
    }
}

}  // namespace lg
