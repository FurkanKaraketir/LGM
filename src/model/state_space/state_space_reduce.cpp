#include "state_space_reduce.h"

#include "state_space_context.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/subs.h>

#include <algorithm>

namespace {

bool isThroughOnlyLinearSum(const SymEngine::RCP<const SymEngine::Basic>& expr,
                            const std::unordered_set<QString>& throughNames) {
    using SymEngine::Add;
    using SymEngine::Basic;
    using SymEngine::Integer;
    using SymEngine::Mul;
    using SymEngine::Symbol;
    using SymEngine::down_cast;
    using SymEngine::eq;
    using SymEngine::expand;
    using SymEngine::integer;

    const SymEngine::RCP<const Basic> e = expand(expr);
    if (eq(*e, *integer(0))) {
        return true;
    }
    if (SymEngine::is_a<Symbol>(*e)) {
        return throughNames.count(
                   QString::fromStdString(down_cast<const Symbol&>(*e).get_name())) != 0;
    }
    if (SymEngine::is_a<Mul>(*e)) {
        const auto& args = down_cast<const Mul&>(*e).get_args();
        if (args.size() != 2) {
            return false;
        }
        const bool firstInt = SymEngine::is_a<Integer>(*args[0]);
        const bool secondInt = SymEngine::is_a<Integer>(*args[1]);
        if (firstInt == secondInt) {
            return false;
        }
        const SymEngine::RCP<const Basic>& symArg = firstInt ? args[1] : args[0];
        if (!SymEngine::is_a<Symbol>(*symArg)) {
            return false;
        }
        return throughNames.count(QString::fromStdString(
                   down_cast<const Symbol&>(*symArg).get_name())) != 0;
    }
    if (SymEngine::is_a<Add>(*e)) {
        for (const SymEngine::RCP<const Basic>& arg : down_cast<const Add&>(*e).get_args()) {
            if (!isThroughOnlyLinearSum(arg, throughNames)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool isTwigContinuityAlias(const SymEngine::RCP<const SymEngine::Basic>& expr,
                           const std::vector<BranchItem*>& branches) {
    std::unordered_set<QString> throughNames;
    for (BranchItem* branch : branches) {
        if (!branch) {
            continue;
        }
        const QString through = lg::branchThroughSymbol(*branch);
        if (!through.isEmpty()) {
            throughNames.insert(through);
        }
    }
    return isThroughOnlyLinearSum(expr, throughNames);
}

std::vector<lg::ss::RCP<const SymEngine::Basic>> mergeWithFlow(
    const std::vector<lg::ss::RCP<const SymEngine::Basic>>& base,
    const std::vector<lg::ss::RCP<const SymEngine::Basic>>& flow) {
    std::vector<lg::ss::RCP<const SymEngine::Basic>> merged = base;
    merged.insert(merged.end(), flow.begin(), flow.end());
    return merged;
}

}  // namespace

namespace lg {

std::vector<ss::RCP<const ss::Basic>> ReductionRelations::primaryWithFlow(
    const std::vector<ss::RCP<const ss::Basic>>* constraintsOverride) const {
    const std::vector<ss::RCP<const ss::Basic>>& c =
        constraintsOverride ? *constraintsOverride : constraints;
    std::vector<ss::RCP<const ss::Basic>> base = algebraics;
    base.insert(base.end(), matchedAlgebraics.begin(), matchedAlgebraics.end());
    base.insert(base.end(), c.begin(), c.end());
    return mergeWithFlow(base, stateBranchFlowRelations);
}

std::vector<ss::RCP<const ss::Basic>> ReductionRelations::reducedWithFlow(
    const std::vector<ss::RCP<const ss::Basic>>* constraintsOverride) const {
    const std::vector<ss::RCP<const ss::Basic>>& c =
        constraintsOverride ? *constraintsOverride : constraints;
    std::vector<ss::RCP<const ss::Basic>> base = reducedElementals;
    base.insert(base.end(), c.begin(), c.end());
    return mergeWithFlow(base, stateBranchFlowRelations);
}

ReductionRelations buildReductionRelations(const StateSpaceContext& ctx,
                                           const ReductionBuildInput& input) {
    using SymEngine::mul;
    using SymEngine::sub;

    ReductionRelations rels;
    rels.algebraics = input.algebraics;
    rels.matchedAlgebraics = input.matchedAlgebraics;
    rels.constraints = ss::constraintRelations(ctx.replacements);

    rels.reducedElementals.reserve(input.reducedElementals.size());
    for (size_t i = 0; i < ctx.elementalEquations.size(); ++i) {
        BranchItem* branch = ctx.elementalEquations[i].branch;
        if (branch && input.stateMatchedBranches.count(branch) != 0) {
            continue;
        }
        rels.reducedElementals.push_back(input.reducedElementals[i]);
    }

    for (const ComputedState& state : ctx.computedStates) {
        if (!state.branch || state.branch->isActive() ||
            isTwoPortInternalBranch(*state.branch)) {
            continue;
        }
        const bool inTree = ctx.treeSet.count(state.branch) != 0;
        const SystemType systemType = lg::branchSystemType(*state.branch);
        const ss::RCP<const SymEngine::Basic> k = branchElementConstantExpr(*state.branch);
        if (effectivePassiveBranchType(*state.branch) == BranchType::A && inTree) {
            const ss::RCP<const SymEngine::Basic> coeff =
                elementalConstantCoeff(systemType, BranchType::A, k);
            const QString flow = branchThroughSymbol(*state.branch);
            rels.stateBranchFlowRelations.push_back(
                sub(ss::symOf(flow), mul(coeff, ss::dotSym(state.symbol))));
        } else if (effectivePassiveBranchType(*state.branch) == BranchType::T && !inTree) {
            const ss::RCP<const SymEngine::Basic> coeff =
                elementalConstantCoeff(systemType, BranchType::T, k);
            const ss::RCP<const SymEngine::Basic> across = branchNodeAcrossExpr(*state.branch);
            rels.stateBranchFlowRelations.push_back(
                sub(ss::dotSym(state.symbol), mul(coeff, across)));
        }
    }

    return rels;
}

ReductionRelations filterRelationsForOutput(const ReductionRelations& rels,
                                            const StateSpaceContext& ctx, const QString& out) {
    using SymEngine::expand;
    using SymEngine::sub;

    ReductionRelations filtered = rels;
    filtered.constraints.clear();
    filtered.constraints.reserve(rels.constraints.size());
    const std::unordered_map<QString, ss::RCP<const SymEngine::Basic>> resolved =
        ss::resolveReplacements(ctx.replacements);
    for (const auto& [name, expr] : resolved) {
        if (name == out || !isValidVariableSymbol(name)) {
            continue;
        }
        if (isTwigContinuityAlias(expr, ctx.branches)) {
            continue;
        }
        filtered.constraints.push_back(expand(sub(ss::symOf(name), expr)));
    }
    return filtered;
}

void ssReduceExpressions(StateSpaceContext& ctx,
                         std::unordered_map<QString, ss::RCP<const ss::Basic>>& targets,
                         const ReductionRelations& rels, const ReductionSubs& subMaps,
                         const std::vector<QString>& nodeAcrossSymbols,
                         const std::function<bool(const QString&)>& canEliminate,
                         const ReductionOptions& opts, const std::vector<QString>& stateSymbols) {
    using SymEngine::eq;
    using SymEngine::expand;
    using SymEngine::subs;

    const QString prefix = opts.phasePrefix;
    const auto phase = [&](const QString& name) {
        return prefix.isEmpty() ? name : prefix + name;
    };

    const std::vector<ss::RCP<const SymEngine::Basic>> primaryRelations =
        rels.primaryWithFlow();
    const std::vector<ss::RCP<const SymEngine::Basic>> reducedRelations =
        rels.reducedWithFlow();
    const std::vector<ss::RCP<const SymEngine::Basic>> postCouplingRelations =
        rels.primaryWithFlow();
    const std::vector<ss::RCP<const SymEngine::Basic>> reducedPostCouplingRelations =
        rels.reducedWithFlow();

    ss::eliminateBranchSymbolsInto(targets, primaryRelations, ctx.branches, ctx.treeSet,
                                   canEliminate, opts.acceptSubstitution,
                                   phase(QStringLiteral("elim_branch_1")));
    ss::eliminateSymbolsInto(targets, primaryRelations, nodeAcrossSymbols, canEliminate, {},
                             opts.acceptSubstitution, phase(QStringLiteral("elim_node_1")));

    ss::ssLog(phase(QStringLiteral("value_sub")));
    for (auto& [name, expr] : targets) {
        (void)name;
        expr = expand(subs(expr, subMaps.valueSubMap));
        expr = expand(subs(expr, subMaps.dependentDotMap));
    }

    ss::eliminateBranchSymbolsInto(targets, reducedRelations, ctx.branches, ctx.treeSet,
                                   canEliminate, opts.acceptSubstitution,
                                   phase(QStringLiteral("elim_branch_2")));
    ss::eliminateSymbolsInto(targets, reducedRelations, nodeAcrossSymbols, canEliminate, {},
                             opts.acceptSubstitution, phase(QStringLiteral("elim_node_2")));

    ss::ssLog(phase(QStringLiteral("dependent_dot_sub")));
    for (auto& [name, expr] : targets) {
        (void)name;
        expr = expand(subs(expr, subMaps.dependentDotMap));
    }

    if (opts.runCoupling && !stateSymbols.empty()) {
        ss::ssLog(phase(QStringLiteral("pre_coupling")),
                  QStringLiteral("targets=%1").arg(targets.size()));
        for (const QString& symbol : stateSymbols) {
            const auto it = targets.find(symbol);
            if (it != targets.end()) {
                ss::ssLog(phase(QStringLiteral("state_dot")),
                          QStringLiteral("%1 = %2")
                              .arg(ss::dotName(symbol), ss::exprText(it->second)));
            }
        }
        ss::resolveStateDotCoupling(stateSymbols, targets);
    }

    ss::ssLog(phase(QStringLiteral("post_coupling_dot_sub")));
    for (auto& [name, expr] : targets) {
        (void)name;
        expr = expand(subs(expr, subMaps.dependentDotMap));
    }

    const std::vector<ss::RCP<const SymEngine::Basic>> allReductionRelations = reducedRelations;
    ss::eliminateSymbolsInto(targets, reducedPostCouplingRelations, nodeAcrossSymbols,
                             canEliminate, {}, opts.acceptSubstitution,
                             phase(QStringLiteral("elim_node_post_coupling")));
    ss::eliminateBranchSymbolsInto(targets, postCouplingRelations, ctx.branches, ctx.treeSet,
                                   canEliminate, opts.acceptSubstitution,
                                   phase(QStringLiteral("elim_branch_post_coupling")));

    for (size_t pass = 0; pass < 4; ++pass) {
        ss::ssLog(phase(QStringLiteral("refine_pass")), QString::number(pass));
        bool changed = false;
        for (auto& [name, expr] : targets) {
            (void)name;
            // ponytail: valueSubMap omitted here — it aliases tree ports (T4=-T_I2) and
            // reapplying it each pass fights branch elimination and decays coefficients.
            const ss::RCP<const SymEngine::Basic> next = expand(subs(expr, subMaps.dependentDotMap));
            if (!eq(*next, *expr)) {
                expr = next;
                changed = true;
            }
        }
        ss::eliminateBranchSymbolsInto(targets, allReductionRelations, ctx.branches, ctx.treeSet,
                                       canEliminate, opts.acceptSubstitution,
                                       phase(QStringLiteral("elim_branch_refine_%1").arg(pass)));
        ss::eliminateSymbolsInto(targets, allReductionRelations, nodeAcrossSymbols,
                                 canEliminate, {}, opts.acceptSubstitution,
                                 phase(QStringLiteral("elim_node_refine_%1").arg(pass)));
        ss::eliminateBranchSymbolsInto(targets, postCouplingRelations, ctx.branches, ctx.treeSet,
                                       canEliminate, opts.acceptSubstitution,
                                       phase(QStringLiteral("elim_branch_refine_post_%1").arg(pass)));
        ss::eliminateSymbolsInto(targets, postCouplingRelations, nodeAcrossSymbols,
                                 canEliminate, {}, opts.acceptSubstitution,
                                 phase(QStringLiteral("elim_node_refine_post_%1").arg(pass)));
        if (!changed) {
            ss::ssLog(phase(QStringLiteral("refine_pass")),
                      QStringLiteral("%1 converged").arg(pass));
            break;
        }
    }

    if (opts.runCoupling && !stateSymbols.empty()) {
        ss::resolveStateDotCoupling(stateSymbols, targets);
    }

    ss::eliminateBranchSymbolsInto(targets, postCouplingRelations, ctx.branches, ctx.treeSet,
                                   canEliminate, opts.acceptSubstitution,
                                   phase(QStringLiteral("elim_branch_final")));
    ss::eliminateSymbolsInto(targets, reducedPostCouplingRelations, nodeAcrossSymbols,
                             canEliminate, {}, opts.acceptSubstitution,
                             phase(QStringLiteral("elim_node_final")));

    ss::ssLog(phase(QStringLiteral("final_sub")));
    for (auto& [name, expr] : targets) {
        (void)name;
        expr = expand(subs(expr, subMaps.dependentDotMap));
        expr = expand(subs(expr, subMaps.valueSubMap));
    }
}

}  // namespace lg
