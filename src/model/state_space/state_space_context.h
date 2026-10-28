#pragma once

#include "state_space.h"
#include "state_space_detail.h"

#include "elemental_equation.h"

#include <symengine/basic.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lg {

struct ComputedState {
    QString symbol;
    BranchItem* branch = nullptr;
};

struct BranchElemental {
    BranchItem* branch = nullptr;
    ss::RCP<const ss::Basic> expr;
};

struct StateSpaceContext {
    const NormalTreeResult& tree;
    const std::vector<NodeItem*>& nodes;
    const std::vector<BranchItem*>& branches;
    const std::vector<TwoPortItem*>& twoPorts;

    StateSpaceResult result;
    QStringList requestedOutputs;
    std::unordered_set<BranchItem*> treeSet;
    std::vector<ComputedState> computedStates;
    std::vector<BranchElemental> elementalEquations;
    std::vector<QString> timedSymbols;
    std::unordered_map<QString, ss::RCP<const ss::Basic>> outputExprs;

    StateSpaceContext(const NormalTreeResult& t, const std::vector<NodeItem*>& n,
                      const std::vector<BranchItem*>& b, const std::vector<TwoPortItem*>& tp);

    bool initStates();
    void addEquationText(QStringList& list, const QString& lhs, const ss::RCP<const ss::Basic>& rhs);
    void appendTimed(const QString& symbol);
};

// Returns false if result.status set (early exit).
bool ssBuildConstraints(StateSpaceContext& ctx);
// Solves the whole graph (KCL, two-ports, elementals and their time derivatives) for the state
// derivatives and the requested outputs; fills stateDots and ctx.outputExprs.
bool ssDeriveStateDots(StateSpaceContext& ctx, std::unordered_map<QString, ss::RCP<const ss::Basic>>& stateDots);
void ssAssembleMatrix(StateSpaceContext& ctx, const std::unordered_map<QString, ss::RCP<const ss::Basic>>& stateDots);
bool ssAssembleOutputs(StateSpaceContext& ctx);

}  // namespace lg
