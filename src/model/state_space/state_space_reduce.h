#pragma once

#include "state_space_detail.h"

#include <symengine/basic.h>

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BranchItem;

namespace lg {

struct StateSpaceContext;

struct ReductionRelations {
    std::vector<ss::RCP<const ss::Basic>> algebraics;
    std::vector<ss::RCP<const ss::Basic>> matchedAlgebraics;
    std::vector<ss::RCP<const ss::Basic>> reducedElementals;
    std::vector<ss::RCP<const ss::Basic>> stateBranchFlowRelations;
    std::vector<ss::RCP<const ss::Basic>> constraints;

    std::vector<ss::RCP<const ss::Basic>> primaryWithFlow(
        const std::vector<ss::RCP<const ss::Basic>>* constraintsOverride = nullptr) const;
    std::vector<ss::RCP<const ss::Basic>> reducedWithFlow(
        const std::vector<ss::RCP<const ss::Basic>>* constraintsOverride = nullptr) const;
};

struct ReductionBuildInput {
    std::vector<ss::RCP<const ss::Basic>> algebraics;
    std::vector<ss::RCP<const ss::Basic>> matchedAlgebraics;
    std::vector<ss::RCP<const ss::Basic>> reducedElementals;
    std::unordered_set<BranchItem*> stateMatchedBranches;
};

struct ReductionSubs {
    SymEngine::map_basic_basic valueSubMap;
    SymEngine::map_basic_basic dependentDotMap;
};

struct ReductionOptions {
    ss::SubstitutionFilter acceptSubstitution;
    bool runCoupling = false;
    QString phasePrefix;
};

ReductionRelations buildReductionRelations(const StateSpaceContext& ctx,
                                           const ReductionBuildInput& input);

ReductionRelations filterRelationsForOutput(const ReductionRelations& rels,
                                            const StateSpaceContext& ctx, const QString& out);

void ssReduceExpressions(StateSpaceContext& ctx,
                         std::unordered_map<QString, ss::RCP<const ss::Basic>>& targets,
                         const ReductionRelations& rels, const ReductionSubs& subMaps,
                         const std::vector<QString>& nodeAcrossSymbols,
                         const std::function<bool(const QString&)>& canEliminate,
                         const ReductionOptions& opts,
                         const std::vector<QString>& stateSymbols = {});

}  // namespace lg
