#include "state_space_context.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/subs.h>

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace lg {

namespace {

using SymEngine::add;
using SymEngine::expand;
using SymEngine::integer;
using SymEngine::mul;
using SymEngine::sub;
using SymEngine::subs;

ss::RCP<const ss::Basic> nodePotential(const NodeItem& node) {
    return node.isGround() ? ss::RCP<const ss::Basic>(integer(0)) : ss::symOf(node.acrossVariable());
}

ss::RCP<const ss::Basic> branchNodeDifference(const BranchItem& branch) {
    return sub(nodePotential(*branch.from()), nodePotential(*branch.to()));
}

}  // namespace

bool ssDeriveStateDots(StateSpaceContext& ctx,
                       std::unordered_map<QString, ss::RCP<const ss::Basic>>& stateDots) {
    static const bool kSolverChecked = (ss::solveLinearTargetsSelfCheck(), true);
    (void)kSolverChecked;
    auto fail = [&](StateSpaceResult::Status status, const QString& message) {
        ctx.result.status = status;
        ctx.result.message = message;
        ss::ssLog(QStringLiteral("error"), message);
        return false;
    };

    // Time-varying symbols are node potentials, branch flows, states and inputs; any other
    // symbol (R, C, TF, ...) is a parameter.
    std::vector<QString> timed;
    std::unordered_set<QString> timedSet;
    auto addTimed = [&](const QString& name) {
        if (timedSet.insert(name).second) {
            timed.push_back(name);
        }
    };
    std::vector<BranchItem*> connected;
    for (NodeItem* node : ctx.nodes) {
        if (!node || node->isGround()) {
            continue;
        }
        if (!isValidVariableSymbol(node->acrossVariable())) {
            return fail(StateSpaceResult::Status::GraphError,
                        QStringLiteral("Node %1 needs a valid across variable.").arg(node->name()));
        }
        addTimed(node->acrossVariable().trimmed());
    }
    for (BranchItem* branch : ctx.branches) {
        if (!branch || !branch->from() || !branch->to() || branch->from() == branch->to()) {
            continue;
        }
        if (!isValidVariableSymbol(branchThroughSymbol(*branch))) {
            return fail(StateSpaceResult::Status::GraphError,
                        QStringLiteral("Branch %1 needs a valid through variable.").arg(branch->name()));
        }
        connected.push_back(branch);
        addTimed(branchThroughSymbol(*branch));
    }
    std::unordered_set<QString> knowns;
    std::vector<ss::RCP<const ss::Basic>> knownSyms;
    for (const ComputedState& state : ctx.computedStates) {
        addTimed(state.symbol);
        knowns.insert(state.symbol);
        knownSyms.push_back(ss::symOf(state.symbol));
    }
    for (const QString& input : ctx.result.inputs) {
        addTimed(input);
        for (const QString& name : {input, ss::dotName(input)}) {
            knowns.insert(name);
            knownSyms.push_back(ss::symOf(name));
        }
    }
    SymEngine::map_basic_basic dotMap;
    std::unordered_set<QString> unknowns;
    for (const QString& name : timed) {
        dotMap[ss::symOf(name)] = ss::dotSym(name);
        for (const QString& variable : {name, ss::dotName(name)}) {
            if (knowns.count(variable) == 0) {
                unknowns.insert(variable);
            }
        }
    }

    // Algebraic constraints hold at every instant, so their time derivatives hold too; that
    // derivative level is what couples dependent storage (A-type links, T-type twigs).
    std::vector<ss::RCP<const ss::Basic>> algebraic;
    for (NodeItem* node : ctx.nodes) {
        ss::RCP<const ss::Basic> leaving = integer(0);
        for (BranchItem* branch : connected) {
            if (branch->from() == node) {
                leaving = add(leaving, ss::symOf(branchThroughSymbol(*branch)));
            } else if (branch->to() == node) {
                leaving = sub(leaving, ss::symOf(branchThroughSymbol(*branch)));
            }
        }
        algebraic.push_back(leaving);
    }
    for (BranchItem* branch : connected) {
        if (branch->isActive() && branch->branchType() == BranchType::A) {
            algebraic.push_back(
                sub(branchNodeDifference(*branch), ss::symOf(branchSourceInputSymbol(*branch))));
        }
    }
    for (const NormalTreeResult::StateVariable& state : ctx.tree.stateVariables) {
        const bool across = state.kind == NormalTreeResult::StateVariable::Kind::Across;
        algebraic.push_back(sub(ss::symOf(state.symbol),
                                across ? branchNodeDifference(*state.branch)
                                       : ss::symOf(branchThroughSymbol(*state.branch))));
    }
    std::vector<ss::RCP<const ss::Basic>> equations;
    for (const BranchElemental& elemental : ctx.elementalEquations) {
        const bool dynamic = elemental.branch &&
                             effectivePassiveBranchType(*elemental.branch) != BranchType::D;
        (dynamic ? equations : algebraic).push_back(elemental.expr);
    }
    for (const ss::RCP<const ss::Basic>& equation : algebraic) {
        equations.push_back(expand(equation));
        equations.push_back(expand(subs(equation, dotMap)));
    }

    std::vector<QString> targets;
    for (const ComputedState& state : ctx.computedStates) {
        targets.push_back(ss::dotName(state.symbol));
    }
    for (const QString& out : ctx.requestedOutputs) {
        if (knowns.count(out) != 0) {
            continue;
        }
        if (unknowns.count(out) == 0) {
            const auto owner = std::find_if(connected.begin(), connected.end(), [&](BranchItem* branch) {
                return branchAcrossSymbol(*branch) == out;
            });
            if (owner == connected.end() || !isValidVariableSymbol(out)) {
                return fail(StateSpaceResult::Status::Unsupported,
                            QStringLiteral("Output %1 is not a variable of this graph.").arg(out));
            }
            unknowns.insert(out);
            equations.push_back(sub(ss::symOf(out), branchNodeDifference(**owner)));
        }
        targets.push_back(out);
    }

    ss::ssLog(QStringLiteral("solve"), QStringLiteral("equations=%1 unknowns=%2 targets=%3")
                                           .arg(equations.size())
                                           .arg(unknowns.size())
                                           .arg(targets.size()));
    const ss::LinearSolveResult solved = ss::solveLinearTargets(equations, unknowns, targets);
    if (solved.inconsistent) {
        return fail(StateSpaceResult::Status::SymbolicError,
                    QStringLiteral("The graph equations are inconsistent: the sources or the "
                                   "chosen states are not independent."));
    }

    // Each result as sum(coeff * known), every coefficient collapsed to one fraction.
    auto tidy = [&](const QString& name) -> std::optional<ss::RCP<const ss::Basic>> {
        const auto it = solved.values.find(name);
        if (it == solved.values.end() ||
            !ss::isIdenticallyZero(ss::linearRemainder(it->second, knownSyms))) {
            return std::nullopt;
        }
        ss::RCP<const ss::Basic> sum = integer(0);
        for (const ss::RCP<const ss::Basic>& known : knownSyms) {
            const ss::RCP<const ss::Basic> coeff = ss::linearCoeffRCP(it->second, known);
            if (!ss::isIdenticallyZero(coeff)) {
                const ss::RCP<const ss::Basic> simple = ss::simplifyRational(coeff);
                for (const ss::RCP<const ss::Basic>& part :
                     SymEngine::is_a<SymEngine::Add>(*simple) ? simple->get_args()
                                                              : SymEngine::vec_basic{simple}) {
                    sum = add(sum, mul(part, known));
                }
            }
        }
        return sum;
    };
    for (const ComputedState& state : ctx.computedStates) {
        const std::optional<ss::RCP<const ss::Basic>> value = tidy(ss::dotName(state.symbol));
        if (!value) {
            return fail(StateSpaceResult::Status::SymbolicError,
                        QStringLiteral("%1 is not determined by the states and inputs; check the "
                                       "normal tree.")
                            .arg(ss::dotName(state.symbol)));
        }
        stateDots[state.symbol] = *value;
        ctx.result.stateEquations.push_back(
            QStringLiteral("%1 = %2")
                .arg(ss::dotName(state.symbol),
                     ss::displayExprText(*value, ctx.result.inputs, ctx.result.inputLabels)));
        ss::ssLog(QStringLiteral("state_equation"), ctx.result.stateEquations.back());
    }
    for (const QString& out : ctx.requestedOutputs) {
        if (knowns.count(out) != 0) {
            ctx.outputExprs[out] = ss::symOf(out);
            continue;
        }
        const std::optional<ss::RCP<const ss::Basic>> value = tidy(out);
        if (!value) {
            return fail(StateSpaceResult::Status::SymbolicError,
                        QStringLiteral("Output %1 is not determined by the states and inputs.")
                            .arg(out));
        }
        ctx.outputExprs[out] = *value;
    }
    return true;
}

}  // namespace lg
