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

bool ssReflectAndBind(StateSpaceContext& ctx) {
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
    const auto& branches = ctx.branches;
    const auto& twoPorts = ctx.twoPorts;
  ss::ssLog(QStringLiteral("mass_spring_reflect"));
  // ponytail: LGM mass-in-tree + co-tree T-type storage — TF chain reflects link through into f_M
  {
        BranchItem* massBranch = nullptr;
        for (BranchItem* branch : branches) {
            if (!branch || ctx.treeSet.count(branch) == 0) {
                continue;
            }
            const QString constant = branch->elementConstant().trimmed();
            if (constant.isEmpty() || constant.at(0).toUpper() != QLatin1Char('M')) {
                continue;
            }
            if (effectivePassiveBranchType(*branch) != BranchType::A) {
                continue;
            }
            massBranch = branch;
            break;
        }
        if (massBranch) {
            const NodeItem* massAcrossNode = nullptr;
            if (massBranch->from() && !massBranch->from()->isGround()) {
                massAcrossNode = massBranch->from();
            } else if (massBranch->to() && !massBranch->to()->isGround()) {
                massAcrossNode = massBranch->to();
            }
            const ComputedState* complianceState = nullptr;
            for (const ComputedState& state : ctx.computedStates) {
                if (!state.branch || ctx.treeSet.count(state.branch) != 0) {
                    continue;
                }
                if (effectivePassiveBranchType(*state.branch) != BranchType::T) {
                    continue;
                }
                complianceState = &state;
                break;
            }
            const NodeItem* complianceAcrossNode = nullptr;
            if (complianceState && complianceState->branch) {
                if (complianceState->branch->from() &&
                    !complianceState->branch->from()->isGround()) {
                    complianceAcrossNode = complianceState->branch->from();
                } else if (complianceState->branch->to() &&
                           !complianceState->branch->to()->isGround()) {
                    complianceAcrossNode = complianceState->branch->to();
                }
            }
            // ponytail: same across node (B/K/M parallel) — port_span_junction already sums through variables; skip reflect.
            if (massAcrossNode && complianceAcrossNode && complianceState &&
                massAcrossNode != complianceAcrossNode) {
                const std::optional<ss::RCP<const SymEngine::Basic>> tfProduct =
                    ss::transformerModulusProductBetween(massAcrossNode, complianceAcrossNode,
                                                         twoPorts);
                if (tfProduct) {
                    ss::RCP<const SymEngine::Basic> inputSum = integer(0);
                    for (BranchItem* branch : branches) {
                        if (!branch || !branch->isActive() ||
                            branch->branchType() != BranchType::T) {
                            continue;
                        }
                        if (branch->from() != massAcrossNode && branch->to() != massAcrossNode) {
                            continue;
                        }
                        inputSum = add(inputSum, ss::symOf(branchSourceInputSymbol(*branch)));
                    }
                    if (eq(*inputSum, *integer(0)) && !ctx.result.inputs.empty()) {
                        inputSum = ss::symOf(ctx.result.inputs.front());
                    }
                    if (!eq(*inputSum, *integer(0))) {
                        const ss::RCP<const SymEngine::Basic> reflected =
                            div(ss::symOf(complianceState->symbol), *tfProduct);
                        const QString massFlow = branchThroughSymbol(*massBranch);
                        ctx.recordReplacement(massFlow, sub(inputSum, reflected),
                                          ctx.result.continuityEquations);
                    }
                }
            }
        }
    }

    // ponytail: synthetic across state C5_across = V1−V2 — bind free node so V2_dot = Vs1_dot − C5_across_dot
    for (const ComputedState& state : ctx.computedStates) {
        if (!state.branch) {
            continue;
        }
        const QString tautology = ss::branchTautologyAcrossSymbol(*state.branch);
        if (tautology.isEmpty() || state.symbol != tautology) {
            continue;
        }
        const QString acrossText = branchAcrossVariableText(*state.branch).trimmed();
        const int dash = acrossText.indexOf(QStringLiteral(" - "));
        if (dash <= 0) {
            continue;
        }
        const QString hiName = acrossText.left(dash).trimmed();
        const QString loName = acrossText.mid(dash + 3).trimmed();
        if (!isValidVariableSymbol(hiName) || !isValidVariableSymbol(loName) ||
            ctx.replacements.count(loName) != 0 || ctx.isPrimaryState(loName)) {
            continue;
        }
        ss::RCP<const SymEngine::Basic> hiExpr = ss::symOf(hiName);
        {
            const auto resolved = ss::resolveReplacements(ctx.replacements);
            if (const auto it = resolved.find(hiName); it != resolved.end()) {
                hiExpr = it->second;
            }
        }
        ctx.recordConstraint(loName, sub(hiExpr, ss::symOf(state.symbol)));
    }

    for (const QString& input : ctx.result.inputs) {
        ctx.appendTimed(input);
    }
    {
        QStringList timedList;
        for (const QString& s : ctx.timedSymbols) {
            timedList.push_back(s);
        }
        ss::ssLog(QStringLiteral("timed_symbols"), timedList.join(QStringLiteral(", ")));
    }
    return true;
}

}  // namespace lg
