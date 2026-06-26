#include "state_space_context.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/subs.h>

#include <algorithm>
#include <unordered_map>

namespace lg {

#include <symengine/subs.h>
#include <functional>

bool ssDeriveStateDots(StateSpaceContext& ctx,
                       std::unordered_map<QString, ss::RCP<const ss::Basic>>& stateDots) {
    using SymEngine::add;
    using SymEngine::div;
    using SymEngine::eq;
    using SymEngine::expand;
    using SymEngine::integer;
    using SymEngine::mul;
    using SymEngine::neg;
    using SymEngine::sub;
    using SymEngine::subs;
    const auto& tree = ctx.tree;
    const auto& nodes = ctx.nodes;
    const auto& branches = ctx.branches;
    const auto& twoPorts = ctx.twoPorts;
    ss::ssLog(QStringLiteral("substitute_elementals"));
    std::vector<ss::RCP<const SymEngine::Basic>> rawElementals;
    rawElementals.reserve(ctx.elementalEquations.size());
    for (const BranchElemental& elemental : ctx.elementalEquations) {
        rawElementals.push_back(expand(elemental.expr));
    }

    const SymEngine::map_basic_basic valueSubMap =
        ss::substitutionMap(ss::resolveReplacements(ctx.replacements));
    const SymEngine::map_basic_basic subMap = ss::extendedSubstitutionMap(ctx.replacements, ctx.timedSymbols);
    std::vector<ss::RCP<const SymEngine::Basic>> reducedElementals;
    reducedElementals.reserve(ctx.elementalEquations.size());
    for (const BranchElemental& elemental : ctx.elementalEquations) {
        reducedElementals.push_back(expand(subs(elemental.expr, subMap)));
    }

    ss::ssLog(QStringLiteral("ctx.replacements"), QString::number(ctx.replacements.size()));
    {
        QStringList replLines;
        for (const auto& [name, expr] : ss::resolveReplacements(ctx.replacements)) {
            replLines.push_back(QStringLiteral("%1 = %2").arg(name, ss::exprText(expr)));
        }
        ss::ssLog(QStringLiteral("ctx.replacements_resolved"), replLines.join(QStringLiteral("; ")));
    }
    if (!ctx.result.elementalEquations.isEmpty()) {
        ss::ssLog(QStringLiteral("elemental_text"), ctx.result.elementalEquations.join(QStringLiteral("; ")));
    }
    ss::ssLogExprs(QStringLiteral("elemental"), rawElementals);
    ss::ssLogExprs(QStringLiteral("reduced_elemental"), reducedElementals);

    ss::ssLog(QStringLiteral("match_state_dots"));
    std::unordered_set<BranchItem*> stateMatchedBranches;
    std::vector<ss::RCP<const SymEngine::Basic>> algebraics;
    std::vector<ss::RCP<const SymEngine::Basic>> matchedAlgebraics;
    for (const BranchElemental& elemental : ctx.elementalEquations) {
        const ss::RCP<const SymEngine::Basic> reduced = expand(subs(elemental.expr, valueSubMap));
        if (!elemental.branch) {
            algebraics.push_back(reduced);
            continue;
        }
        bool matched = false;
        const QString elementalStateSymbol =
            elemental.branch
                ? ss::storageStateSymbol(*elemental.branch,
                                         ctx.treeSet.count(elemental.branch) != 0)
                : QString();
        for (const ComputedState& state : ctx.computedStates) {
            if (state.branch != elemental.branch &&
                (elementalStateSymbol.isEmpty() || state.symbol != elementalStateSymbol)) {
                continue;
            }
            const ss::RCP<const SymEngine::Basic> dot = ss::dotSym(state.symbol);
            const ss::RCP<const SymEngine::Basic> matchReduced =
                ss::rewriteForSyntheticAcrossStateDot(reduced, *state.branch, state.symbol,
                                                      valueSubMap, ctx.timedSymbols);
            const std::optional<ss::RCP<const SymEngine::Basic>> solved =
                ss::solveLinearFor(matchReduced, dot);
            if (!solved) {
                continue;
            }
            if (stateDots.count(state.symbol) != 0) {
                ss::ssLog(QStringLiteral("warn"),
                          QStringLiteral("duplicate %1 from %2, keeping first")
                              .arg(ss::dotName(state.symbol), elemental.branch->name()));
                matched = true;
                break;
            }
            stateDots[state.symbol] = *solved;
            stateMatchedBranches.insert(elemental.branch);
            ss::ssLog(QStringLiteral("matched"),
                      QStringLiteral("%1 from branch %2: 0 = %3")
                          .arg(ss::dotName(state.symbol), elemental.branch->name(),
                               ss::exprText(reduced)));
            matched = true;
            break;
        }
        if (!matched) {
            algebraics.push_back(reduced);
        } else {
            matchedAlgebraics.push_back(reduced);
        }
    }

    if (stateDots.size() != ctx.computedStates.size()) {
        QStringList missing;
        for (const ComputedState& state : ctx.computedStates) {
            if (stateDots.count(state.symbol) == 0) {
                const QString branchName =
                    state.branch ? state.branch->name() : QStringLiteral("?");
                missing.push_back(QStringLiteral("%1 (%2)").arg(state.symbol, branchName));
            }
        }
        ss::ssLog(QStringLiteral("error"),
                  QStringLiteral("missing state dots: %1").arg(missing.join(QStringLiteral(", "))));
        ss::ssLogExprs(QStringLiteral("algebraic"), algebraics);
        ctx.result.status = StateSpaceResult::Status::SymbolicError;
        ctx.result.message = QStringLiteral("Could not derive all state equations.");
        return false;
    }

    for (const ComputedState& state : ctx.computedStates) {
        ss::ssLog(QStringLiteral("state_dot_initial"),
                  QStringLiteral("%1 = %2")
                      .arg(ss::dotName(state.symbol), ss::exprText(stateDots.at(state.symbol))));
    }

    const auto isStateSymbol = [&](const QString& symbolName) {
        for (const ComputedState& state : ctx.computedStates) {
            if (state.symbol == symbolName) {
                return true;
            }
        }
        return false;
    };
    const auto isInputLike = [&](const QString& symbolName) {
        if (std::find(ctx.result.inputs.begin(), ctx.result.inputs.end(), symbolName) != ctx.result.inputs.end()) {
            return true;
        }
        for (const QString& input : ctx.result.inputs) {
            if (symbolName == ss::dotName(input)) {
                return true;
            }
        }
        return false;
    };
    const auto isStateDotLike = [&](const QString& symbolName) {
        if (!symbolName.endsWith(QStringLiteral("_dot"))) {
            return false;
        }
        return isStateSymbol(symbolName.left(symbolName.size() - 4));
    };
    const auto canEliminateBranchSymbol = [&](const QString& candidate) {
        return !candidate.isEmpty() && candidate != QStringLiteral("0") &&
               isValidVariableSymbol(candidate) && !isStateSymbol(candidate) &&
               !isInputLike(candidate) && !isStateDotLike(candidate);
    };

    const std::vector<QString> nodeAcrossSymbols = ss::collectNodeAcrossSymbols(nodes);
    const std::vector<ss::RCP<const SymEngine::Basic>> constraintEqs =
        ss::constraintRelations(ctx.replacements);
    ss::ssLog(QStringLiteral("constraint_eqs"), QString::number(constraintEqs.size()));
    ss::ssLogExprs(QStringLiteral("constraint"), constraintEqs);
    auto allRelations = [&](const std::vector<ss::RCP<const SymEngine::Basic>>& base) {
        std::vector<ss::RCP<const SymEngine::Basic>> merged = base;
        merged.insert(merged.end(), matchedAlgebraics.begin(), matchedAlgebraics.end());
        merged.insert(merged.end(), constraintEqs.begin(), constraintEqs.end());
        return merged;
    };
    // Reusing a state-branch elemental to eliminate its through/across collapses the dot equation.
    auto stateDotReducedRelations = [&]() {
        std::vector<ss::RCP<const SymEngine::Basic>> merged;
        merged.reserve(reducedElementals.size() + constraintEqs.size());
        for (size_t i = 0; i < ctx.elementalEquations.size(); ++i) {
            BranchItem* branch = ctx.elementalEquations[i].branch;
            if (branch && stateMatchedBranches.count(branch) != 0) {
                continue;
            }
            merged.push_back(reducedElementals[i]);
        }
        merged.insert(merged.end(), constraintEqs.begin(), constraintEqs.end());
        return merged;
    };
    const auto dependentDotMap =
        ss::dependentDotSubstitutionMap(ctx.replacements, ctx.timedSymbols, isStateSymbol);

    const std::vector<ss::RCP<const SymEngine::Basic>> stateBranchFlowRelations = [&]() {
        std::vector<ss::RCP<const SymEngine::Basic>> relations;
        for (const ComputedState& state : ctx.computedStates) {
            if (!state.branch || state.branch->isActive() ||
                isTwoPortInternalBranch(*state.branch)) {
                continue;
            }
            const bool inTree = ctx.treeSet.count(state.branch) != 0;
            if (effectivePassiveBranchType(*state.branch) != BranchType::A || !inTree) {
                continue;
            }
            const SystemType systemType = lg::branchSystemType(*state.branch);
            const ss::RCP<const SymEngine::Basic> k = branchElementConstantExpr(*state.branch);
            const ss::RCP<const SymEngine::Basic> coeff =
                elementalConstantCoeff(systemType, BranchType::A, k);
            const QString flow = branchThroughSymbol(*state.branch);
            relations.push_back(sub(ss::symOf(flow), mul(coeff, ss::dotSym(state.symbol))));
        }
        return relations;
    }();
    const auto withFlowRelations =
        [&](const std::vector<ss::RCP<const SymEngine::Basic>>& base) {
            std::vector<ss::RCP<const SymEngine::Basic>> merged = base;
            merged.insert(merged.end(), stateBranchFlowRelations.begin(),
                          stateBranchFlowRelations.end());
            return merged;
        };

    std::unordered_map<QString, BranchItem*> stateBranchBySymbol;
    for (const ComputedState& state : ctx.computedStates) {
        stateBranchBySymbol[state.symbol] = state.branch;
    }

    const std::vector<ss::RCP<const SymEngine::Basic>> inputPathRelations =
        withFlowRelations(allRelations(algebraics));
    std::unordered_set<QString> inputCarriers;
    if (!ctx.result.inputs.isEmpty()) {
        std::vector<QString> carrierCandidates;
        carrierCandidates.reserve(nodeAcrossSymbols.size() + branches.size() * 2);
        carrierCandidates.insert(carrierCandidates.end(), nodeAcrossSymbols.begin(),
                                 nodeAcrossSymbols.end());
        for (BranchItem* branch : branches) {
            if (!branch) {
                continue;
            }
            for (const QString& symbol :
                 {branchThroughSymbol(*branch), branchAcrossSymbol(*branch)}) {
                if (!symbol.isEmpty()) {
                    carrierCandidates.push_back(symbol);
                }
            }
        }
        const auto markInputCarriers = [&](const ss::RCP<const SymEngine::Basic>& relation) {
            for (const QString& input : ctx.result.inputs) {
                if (eq(*ss::linearCoeffRCP(relation, ss::symOf(input)), *integer(0))) {
                    continue;
                }
                for (const QString& symbol : carrierCandidates) {
                    if (symbol == input) {
                        continue;
                    }
                    if (!eq(*ss::linearCoeffRCP(relation, ss::symOf(symbol)), *integer(0))) {
                        inputCarriers.insert(symbol);
                    }
                }
            }
        };
        for (const ss::RCP<const SymEngine::Basic>& relation : constraintEqs) {
            markInputCarriers(relation);
        }
        for (const ss::RCP<const SymEngine::Basic>& relation : inputPathRelations) {
            markInputCarriers(relation);
        }
    }
    const auto solvedTouchesInputPath = [&](const ss::RCP<const SymEngine::Basic>& solved) {
        for (const QString& input : ctx.result.inputs) {
            if (!eq(*ss::linearCoeffRCP(solved, ss::symOf(input)), *integer(0))) {
                return true;
            }
        }
        for (const QString& carrier : inputCarriers) {
            if (!eq(*ss::linearCoeffRCP(solved, ss::symOf(carrier)), *integer(0))) {
                return true;
            }
        }
        return false;
    };
    const auto inputPathAlternativeExists =
        [&](const ss::RCP<const SymEngine::Basic>& primarySym) {
            for (const ss::RCP<const SymEngine::Basic>& relation : inputPathRelations) {
                const std::optional<ss::RCP<const SymEngine::Basic>> alternative =
                    ss::solveLinearFor(relation, primarySym);
                if (alternative && solvedTouchesInputPath(*alternative)) {
                    return true;
                }
            }
            return false;
        };

    const auto rejectTautologicalNodeElim = [&](const QString& targetName,
                                                const ss::RCP<const SymEngine::Basic>& targetExpr,
                                                const ss::RCP<const SymEngine::Basic>& primarySym,
                                                const ss::RCP<const SymEngine::Basic>& solved) {
        const auto it = stateBranchBySymbol.find(targetName);
        if (it != stateBranchBySymbol.end() && it->second) {
            const QString across = ss::branchTautologyAcrossSymbol(*it->second);
            if (!across.isEmpty()) {
                bool primaryIsAcrossNode = false;
                const QString acrossText = branchAcrossVariableText(*it->second).trimmed();
                const int dash = acrossText.indexOf(QStringLiteral(" - "));
                if (dash > 0) {
                    const QString hiName = acrossText.left(dash).trimmed();
                    const QString loName = acrossText.mid(dash + 3).trimmed();
                    if (isValidVariableSymbol(hiName) &&
                        eq(*primarySym, *ss::symOf(hiName))) {
                        primaryIsAcrossNode = true;
                    }
                    if (isValidVariableSymbol(loName) &&
                        eq(*primarySym, *ss::symOf(loName))) {
                        primaryIsAcrossNode = true;
                    }
                } else if (isValidVariableSymbol(acrossText) &&
                           eq(*primarySym, *ss::symOf(acrossText))) {
                    primaryIsAcrossNode = true;
                }
                // ponytail: branch flows (i_R1) may legitimately depend on state; block node-V only
                if (primaryIsAcrossNode &&
                    !eq(*ss::linearCoeffRCP(solved, ss::symOf(across)), *integer(0))) {
                    return false;
                }
            }
        }
        return !ss::substitutionIsCircular(targetExpr, primarySym, solved);
    };
    const auto rejectPortAliasInverse = [&](const ss::RCP<const SymEngine::Basic>& primarySym,
                                            const ss::RCP<const SymEngine::Basic>& solved) {
        for (const auto& [portFlow, externalFlow] : ctx.replacements) {
            if (!eq(*externalFlow, *primarySym)) {
                continue;
            }
            const std::optional<ss::RCP<const SymEngine::Basic>> portSym = ss::trySymOf(portFlow);
            if (portSym && eq(*solved, **portSym)) {
                return true;
            }
        }
        return false;
    };
    const auto acceptStateDotSubstitution = [&](const QString& targetName,
                                                  const ss::RCP<const SymEngine::Basic>& targetExpr,
                                                  const ss::RCP<const SymEngine::Basic>& primarySym,
                                                  const ss::RCP<const SymEngine::Basic>& solved) {
        if (rejectPortAliasInverse(primarySym, solved)) {
            return false;
        }
        if (!rejectTautologicalNodeElim(targetName, targetExpr, primarySym, solved)) {
            return false;
        }
        const ss::RCP<const SymEngine::Basic> next =
            expand(subs(targetExpr, {{primarySym, solved}}));
        if (eq(*next, *ss::dotSym(targetName))) {
            return false;
        }
        const auto stateIt = stateBranchBySymbol.find(targetName);
        if (stateIt != stateBranchBySymbol.end() && stateIt->second) {
            const QString stateFlow = branchThroughSymbol(*stateIt->second);
            const std::optional<ss::RCP<const SymEngine::Basic>> stateFlowSym =
                ss::trySymOf(stateFlow);
            if (stateFlowSym && eq(*primarySym, **stateFlowSym)) {
                const ss::RCP<const SymEngine::Basic> ownDot = ss::dotSym(targetName);
                const ss::RCP<const SymEngine::Basic> coeffBefore =
                    ss::linearCoeffRCP(targetExpr, ownDot);
                const ss::RCP<const SymEngine::Basic> coeffAfter =
                    ss::linearCoeffRCP(next, ownDot);
                if (!eq(*coeffBefore, *coeffAfter)) {
                    return false;
                }
            }
        }
        for (const QString& input : ctx.result.inputs) {
            const ss::RCP<const SymEngine::Basic> inputSym = ss::symOf(input);
            const bool hadInput =
                !eq(*ss::linearCoeffRCP(targetExpr, inputSym), *integer(0));
            const bool hasInput = !eq(*ss::linearCoeffRCP(next, inputSym), *integer(0));
            if (hadInput && !hasInput) {
                return false;
            }
            const ss::RCP<const SymEngine::Basic> inputDotSym = ss::symOf(ss::dotName(input));
            const bool hadInputDot =
                !eq(*ss::linearCoeffRCP(targetExpr, inputDotSym), *integer(0));
            const bool hasInputDot =
                !eq(*ss::linearCoeffRCP(next, inputDotSym), *integer(0));
            if (hadInputDot && !hasInputDot) {
                return false;
            }
        }
        const auto it = stateBranchBySymbol.find(targetName);
        if (it == stateBranchBySymbol.end() || !it->second) {
            return true;
        }
        const QString flow = branchThroughSymbol(*it->second);
        if (flow.isEmpty() || flow == targetName) {
            return true;
        }
        const std::optional<ss::RCP<const SymEngine::Basic>> flowSymOpt = ss::trySymOf(flow);
        if (flowSymOpt && eq(*primarySym, **flowSymOpt) && !inputCarriers.empty() &&
            !solvedTouchesInputPath(solved) && inputPathAlternativeExists(primarySym)) {
            return false;
        }
        if (eq(*ss::linearCoeffRCP(next, ss::symOf(flow)), *integer(0))) {
            return true;
        }
        for (const QString& input : ctx.result.inputs) {
            if (!eq(*ss::linearCoeffRCP(next, ss::symOf(input)), *integer(0))) {
                return true;
            }
            if (!eq(*ss::linearCoeffRCP(next, ss::symOf(ss::dotName(input))), *integer(0))) {
                return true;
            }
        }
        std::vector<ss::RCP<const SymEngine::Basic>> otherStates;
        otherStates.reserve(ctx.computedStates.size());
        for (const ComputedState& state : ctx.computedStates) {
            if (state.symbol != targetName) {
                otherStates.push_back(ss::symOf(state.symbol));
            }
        }
        const ss::RCP<const SymEngine::Basic> remainder = ss::linearRemainder(next, otherStates);
        const ss::RCP<const SymEngine::Basic> flowSym = ss::symOf(flow);
        if (!eq(*ss::linearCoeffRCP(remainder, flowSym), *integer(0)) &&
            eq(*ss::linearRemainder(remainder, {flowSym}), *integer(0))) {
            // Allow folding a continuity/compatibility alias (e.g. T4=-T_I2 on inertia branch).
            const std::unordered_map<QString, ss::RCP<const SymEngine::Basic>> resolvedRepl =
                ss::resolveReplacements(ctx.replacements);
            for (const auto& [name, expr] : resolvedRepl) {
                const std::optional<ss::RCP<const SymEngine::Basic>> nameSym = ss::trySymOf(name);
                if (nameSym && eq(*primarySym, **nameSym) && eq(*solved, *expr)) {
                    return true;
                }
            }
            return false;
        }
        return true;
    };

    ss::eliminateBranchSymbolsInto(stateDots, withFlowRelations(allRelations(algebraics)), branches,
                                   ctx.treeSet, canEliminateBranchSymbol, acceptStateDotSubstitution,
                                   QStringLiteral("elim_branch_1"));
    ss::eliminateSymbolsInto(stateDots, withFlowRelations(allRelations(algebraics)),
                             nodeAcrossSymbols, canEliminateBranchSymbol, {},
                             acceptStateDotSubstitution, QStringLiteral("elim_node_1"));

    ss::ssLog(QStringLiteral("value_sub"));
    for (auto& [name, expr] : stateDots) {
        (void)name;
        expr = expand(subs(expr, valueSubMap));
        expr = expand(subs(expr, dependentDotMap));
    }
    for (const ComputedState& state : ctx.computedStates) {
        ss::ssLog(QStringLiteral("state_dot_after_value_sub"),
                  QStringLiteral("%1 = %2")
                      .arg(ss::dotName(state.symbol), ss::exprText(stateDots.at(state.symbol))));
    }
    ss::eliminateBranchSymbolsInto(stateDots, withFlowRelations(stateDotReducedRelations()),
                                   branches, ctx.treeSet, canEliminateBranchSymbol,
                                   acceptStateDotSubstitution, QStringLiteral("elim_branch_2"));
    ss::eliminateSymbolsInto(stateDots, withFlowRelations(stateDotReducedRelations()),
                             nodeAcrossSymbols, canEliminateBranchSymbol, {},
                             acceptStateDotSubstitution, QStringLiteral("elim_node_2"));

    ss::ssLog(QStringLiteral("dependent_dot_sub"));
    for (auto& [name, expr] : stateDots) {
        (void)name;
        expr = expand(subs(expr, dependentDotMap));
    }

    std::vector<QString> stateSymbols;
    stateSymbols.reserve(ctx.computedStates.size());
    for (const ComputedState& state : ctx.computedStates) {
        stateSymbols.push_back(state.symbol);
    }

    ss::ssLog(QStringLiteral("pre_coupling"), QStringLiteral("algebraics=%1").arg(algebraics.size()));
    for (const ComputedState& state : ctx.computedStates) {
        ss::ssLog(QStringLiteral("state_dot"),
                  QStringLiteral("%1 = %2")
                      .arg(ss::dotName(state.symbol), ss::exprText(stateDots.at(state.symbol))));
    }

    ss::resolveStateDotCoupling(stateSymbols, stateDots);

    ss::ssLog(QStringLiteral("post_coupling_dot_sub"));
    for (auto& [name, expr] : stateDots) {
        (void)name;
        expr = expand(subs(expr, dependentDotMap));
    }

    const std::vector<ss::RCP<const SymEngine::Basic>> allReductionRelations =
        withFlowRelations(stateDotReducedRelations());
    const std::vector<ss::RCP<const SymEngine::Basic>> postCouplingRelations =
        withFlowRelations(allRelations(algebraics));
    ss::eliminateSymbolsInto(stateDots, postCouplingRelations, nodeAcrossSymbols,
                             canEliminateBranchSymbol, {}, acceptStateDotSubstitution,
                             QStringLiteral("elim_node_post_coupling"));
    ss::eliminateBranchSymbolsInto(stateDots, postCouplingRelations, branches, ctx.treeSet,
                                   canEliminateBranchSymbol, acceptStateDotSubstitution,
                                   QStringLiteral("elim_branch_post_coupling"));
    for (size_t pass = 0; pass < 4; ++pass) {
        ss::ssLog(QStringLiteral("refine_pass"), QString::number(pass));
        bool changed = false;
        for (auto& [name, expr] : stateDots) {
            (void)name;
            // ponytail: valueSubMap omitted here — it aliases tree ports (T4=-T_I2) and
            // reapplying it each pass fights branch elimination and decays coefficients.
            const ss::RCP<const SymEngine::Basic> next = expand(subs(expr, dependentDotMap));
            if (!eq(*next, *expr)) {
                expr = next;
                changed = true;
            }
        }
        ss::eliminateBranchSymbolsInto(stateDots, allReductionRelations, branches, ctx.treeSet,
                                       canEliminateBranchSymbol, acceptStateDotSubstitution,
                                       QStringLiteral("elim_branch_refine_%1").arg(pass));
        ss::eliminateSymbolsInto(stateDots, allReductionRelations, nodeAcrossSymbols,
                                 canEliminateBranchSymbol, {}, acceptStateDotSubstitution,
                                 QStringLiteral("elim_node_refine_%1").arg(pass));
        ss::eliminateBranchSymbolsInto(stateDots, postCouplingRelations, branches, ctx.treeSet,
                                       canEliminateBranchSymbol, acceptStateDotSubstitution,
                                       QStringLiteral("elim_branch_refine_post_%1").arg(pass));
        ss::eliminateSymbolsInto(stateDots, postCouplingRelations, nodeAcrossSymbols,
                                 canEliminateBranchSymbol, {}, acceptStateDotSubstitution,
                                 QStringLiteral("elim_node_refine_post_%1").arg(pass));
        if (!changed) {
            ss::ssLog(QStringLiteral("refine_pass"), QStringLiteral("%1 converged").arg(pass));
            break;
        }
    }
    ss::resolveStateDotCoupling(stateSymbols, stateDots);
    ss::eliminateBranchSymbolsInto(stateDots, postCouplingRelations, branches, ctx.treeSet,
                                   canEliminateBranchSymbol, acceptStateDotSubstitution,
                                   QStringLiteral("elim_branch_final"));
    ss::eliminateSymbolsInto(stateDots, postCouplingRelations, nodeAcrossSymbols,
                             canEliminateBranchSymbol, {}, acceptStateDotSubstitution,
                             QStringLiteral("elim_node_final"));
    ss::ssLog(QStringLiteral("final_sub"));
    for (auto& [name, expr] : stateDots) {
        (void)name;
        expr = expand(subs(expr, dependentDotMap));
        expr = expand(subs(expr, valueSubMap));
    }

    std::vector<ss::RCP<const SymEngine::Basic>> stateInputVars;
    stateInputVars.reserve(stateSymbols.size() + 2 * static_cast<size_t>(ctx.result.inputs.size()));
    for (const QString& symbolName : stateSymbols) {
        stateInputVars.push_back(ss::symOf(symbolName));
    }
    for (const QString& input : ctx.result.inputs) {
        stateInputVars.push_back(ss::symOf(input));
        stateInputVars.push_back(ss::symOf(ss::dotName(input)));
    }
    for (const ComputedState& state : ctx.computedStates) {
        const ss::RCP<const SymEngine::Basic> remainder =
            ss::linearRemainder(stateDots.at(state.symbol), stateInputVars);
        if (!eq(*remainder, *integer(0))) {
            ctx.result.status = StateSpaceResult::Status::SymbolicError;
            ctx.result.message = QStringLiteral("State equation for %1 has unresolved terms: %2.")
                                 .arg(ss::dotName(state.symbol), ss::exprText(remainder));
            ss::ssLog(QStringLiteral("error"), ctx.result.message);
            ss::ssLog(QStringLiteral("state_dot_full"),
                      QStringLiteral("%1 = %2")
                          .arg(ss::dotName(state.symbol), ss::exprText(stateDots.at(state.symbol))));
            return false;
        }
    }

    ss::ssLog(QStringLiteral("ok"), QStringLiteral("order=%1").arg(ctx.computedStates.size()));

    for (const ComputedState& state : ctx.computedStates) {
        const QString lhs = ss::dotName(state.symbol);
        const QString rhs = ss::displayExprText(stateDots.at(state.symbol), ctx.result.inputs,
                                                ctx.result.inputLabels);
        ctx.result.stateEquations.push_back(QStringLiteral("%1 = %2").arg(lhs, rhs));
        ss::ssLog(QStringLiteral("state_equation"), QStringLiteral("%1 = %2").arg(lhs, rhs));
    }
    return true;
}

}  // namespace lg
