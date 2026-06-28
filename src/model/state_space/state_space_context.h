#pragma once

#include "state_space.h"
#include "state_space_detail.h"
#include "state_space_reduce.h"

#include "elemental_equation.h"

#include <symengine/basic.h>
#include <optional>
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
    std::unordered_map<QString, ss::RCP<const ss::Basic>> replacements;
    std::vector<ss::RCP<const ss::Basic>> constraintEquations;
    std::vector<BranchElemental> elementalEquations;
    std::vector<QString> timedSymbols;
    std::optional<ReductionRelations> reductionRelations;

    StateSpaceContext(const NormalTreeResult& t, const std::vector<NodeItem*>& n,
                      const std::vector<BranchItem*>& b, const std::vector<TwoPortItem*>& tp);

    bool initStates();
    void addEquationText(QStringList& list, const QString& lhs, const ss::RCP<const ss::Basic>& rhs);
    void recordReplacement(const QString& secondary, const ss::RCP<const ss::Basic>& expr,
                           QStringList& list);
    void recordConstraint(const QString& secondary, const ss::RCP<const ss::Basic>& expr,
                          bool overwrite = false);
    void appendTimed(const QString& symbol);
    bool isPrimaryState(const QString& symbolName) const;
};

// Returns false if result.status set (early exit).
bool ssBuildConstraints(StateSpaceContext& ctx);
bool ssReflectAndBind(StateSpaceContext& ctx);
bool ssDeriveStateDots(StateSpaceContext& ctx, std::unordered_map<QString, ss::RCP<const ss::Basic>>& stateDots);
void ssAssembleMatrix(StateSpaceContext& ctx, const std::unordered_map<QString, ss::RCP<const ss::Basic>>& stateDots);
bool ssAssembleOutputs(StateSpaceContext& ctx,
                       const std::unordered_map<QString, ss::RCP<const ss::Basic>>& stateDots);

}  // namespace lg
