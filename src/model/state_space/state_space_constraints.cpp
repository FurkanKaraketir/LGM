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

bool ssBuildConstraints(StateSpaceContext& ctx) {
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

    ctx.timedSymbols = ss::collectNodeAcrossSymbols(nodes);
    for (const ComputedState& state : ctx.computedStates) {
        ctx.appendTimed(state.symbol);
    }
    for (BranchItem* branch : branches) {
        if (!branch || !branch->isActive()) {
            continue;
        }
        ctx.appendTimed(branchSourceInputSymbol(*branch));
    }

    ss::ssLog(QStringLiteral("build_elementals"),
              QStringLiteral("passive_branches=%1").arg(branches.size()));
    for (BranchItem* branch : branches) {
        if (!branch || branch->isActive() || isTwoPortInternalBranch(*branch)) {
            continue;
        }
        const ss::RCP<const SymEngine::Basic> k = branchElementConstantExpr(*branch);
        const SystemType systemType = lg::branchSystemType(*branch);
        const BranchType type = effectivePassiveBranchType(*branch);
        const ss::RCP<const SymEngine::Basic> coeff = elementalConstantCoeff(systemType, type, k);
        const QString flowSymbol = branchThroughSymbol(*branch);
        const ss::RCP<const SymEngine::Basic> flow = ss::symOf(flowSymbol);
        const ss::RCP<const SymEngine::Basic> across = branchNodeAcrossExpr(*branch);
        const ss::RCP<const SymEngine::Basic> acrossDot =
            ss::linearTimeDerivative(across, ctx.timedSymbols);

        switch (type) {
        case BranchType::A:
            ctx.elementalEquations.push_back({branch, sub(flow, mul(coeff, acrossDot))});
            ctx.addEquationText(ctx.result.elementalEquations, flowSymbol, mul(k, acrossDot));
            break;
        case BranchType::T: {
            const ss::RCP<const SymEngine::Basic> displayCoeff =
                elementalConstantInNumerator(systemType, BranchType::T) ? div(integer(1), k) : k;
            ctx.elementalEquations.push_back(
                {branch, sub(ss::dotSym(flowSymbol), mul(coeff, across))});
            ctx.addEquationText(ctx.result.elementalEquations, branchAcrossVariableText(*branch),
                            mul(displayCoeff, ss::dotSym(flowSymbol)));
            break;
        }
        case BranchType::D:
            ctx.elementalEquations.push_back({branch, sub(flow, mul(coeff, across))});
            ctx.addEquationText(ctx.result.elementalEquations, flowSymbol, mul(coeff, across));
            break;
        }
    }

    ss::ssLog(QStringLiteral("two_port_elementals"), QString::number(twoPorts.size()));
    for (TwoPortItem* twoPort : twoPorts) {
        if (!twoPort) {
            continue;
        }
        BranchItem* left = twoPort->leftBranch();
        BranchItem* right = twoPort->rightBranch();
        if (!left || !right) {
            ctx.result.status = StateSpaceResult::Status::GraphError;
            ctx.result.message = QStringLiteral("A two-port element has missing branches.");
            return false;
        }
        const ss::RCP<const SymEngine::Basic> k = twoPortModulusExpr(*twoPort);
        const ss::RCP<const SymEngine::Basic> leftAcross = branchNodeAcrossExpr(*left);
        const ss::RCP<const SymEngine::Basic> rightAcross = branchNodeAcrossExpr(*right);
        const ss::RCP<const SymEngine::Basic> leftThrough = ss::symOf(branchThroughSymbol(*left));
        const ss::RCP<const SymEngine::Basic> rightThrough = ss::symOf(branchThroughSymbol(*right));
        const QString leftAcrossName = branchAcrossVariableText(*left);
        const QString rightAcrossName = branchAcrossVariableText(*right);
        const QString leftThroughName = branchThroughSymbol(*left);
        const QString rightThroughName = branchThroughSymbol(*right);
        if (twoPort->kind() == TwoPortKind::Transformer) {
            ctx.elementalEquations.push_back({nullptr, sub(leftAcross, mul(k, rightAcross))});
            ctx.elementalEquations.push_back({nullptr, sub(leftThrough, div(neg(rightThrough), k))});
            ctx.result.elementalEquations.push_back(
                QStringLiteral("%1 = %2; %3 = %4")
                    .arg(leftAcrossName, ss::exprText(mul(k, rightAcross)), leftThroughName,
                         ss::exprText(div(neg(rightThrough), k))));
        } else {
            ctx.elementalEquations.push_back({nullptr, sub(leftAcross, mul(k, rightThrough))});
            ctx.elementalEquations.push_back({nullptr, sub(leftThrough, div(neg(rightAcross), k))});
            ctx.result.elementalEquations.push_back(
                QStringLiteral("%1 = %2; %3 = %4")
                    .arg(leftAcrossName, ss::exprText(mul(k, rightThrough)), leftThroughName,
                         ss::exprText(div(neg(rightAcross), k))));
        }
    }

    for (BranchItem* branch : branches) {
        if (!branch || !branch->isActive()) {
            continue;
        }
        const bool inTree = ctx.treeSet.count(branch) != 0;
        if (branch->branchType() == BranchType::A && inTree) {
            const QString symbol = branchSourceInputSymbol(*branch);
            ctx.result.inputs.push_back(symbol);
            ctx.result.inputLabels.push_back(branchSourceInputDisplay(*branch));
        } else if (branch->branchType() == BranchType::T && !inTree) {
            const QString symbol = branchSourceInputSymbol(*branch);
            ctx.result.inputs.push_back(symbol);
            ctx.result.inputLabels.push_back(branchSourceInputDisplay(*branch));
        }
    }

    if (!ctx.result.inputs.isEmpty()) {
        ss::ssLog(QStringLiteral("inputs"), ctx.result.inputs.join(QStringLiteral(", ")));
    }

    ss::ssLog(QStringLiteral("continuity_cuts"), QString::number(tree.treeBranches.size()));
    for (BranchItem* twig : tree.treeBranches) {
        if (!twig) {
            continue;
        }
        NodeItem* root = twig->from();
        if (!root || !twig->to()) {
            ctx.result.status = StateSpaceResult::Status::GraphError;
            ctx.result.message = QStringLiteral("A tree branch has missing endpoints.");
            return false;
        }
        // Fundamental cut set of the twig: every branch crossing it, port bonds included.
        const std::unordered_set<NodeItem*> side =
            ss::reachableTreeNodes(root, tree.treeBranches, twig);
        ss::RCP<const SymEngine::Basic> cutSum = integer(0);
        for (BranchItem* branch : branches) {
            if (!branch || branch == twig || !branch->from() || !branch->to()) {
                continue;
            }
            const bool fromIn = side.count(branch->from()) != 0;
            const bool toIn = side.count(branch->to()) != 0;
            if (fromIn != toIn) {
                cutSum = add(cutSum, fromIn ? ss::signedThrough(branch, branch->from(), branch->to())
                                            : ss::signedThrough(branch, branch->to(), branch->from()));
            }
        }
        const QString twigFlowSymbol = branchThroughSymbol(*twig);
        const ss::RCP<const SymEngine::Basic> twigFlow = ss::symOf(twigFlowSymbol);
        const std::optional<ss::RCP<const SymEngine::Basic>> solved =
            ss::solveLinearFor(add(twigFlow, cutSum), twigFlow);
        if (!solved) {
            ctx.result.status = StateSpaceResult::Status::SymbolicError;
            ctx.result.message =
                QStringLiteral("Could not solve continuity equation for %1.").arg(twig->name());
            ss::ssLog(QStringLiteral("error"), ctx.result.message);
            return false;
        }
        ctx.addEquationText(ctx.result.continuityEquations, twigFlowSymbol, *solved);
    }

    ss::ssLog(QStringLiteral("compatibility"));
    for (BranchItem* branch : branches) {
        if (!branch || !branch->isActive() || branch->branchType() != BranchType::A ||
            ctx.treeSet.count(branch) == 0 || !branch->from() || !branch->to()) {
            continue;
        }
        // Same convention as every branch: u = across(from) - across(to).
        const ss::RCP<const SymEngine::Basic> input = ss::symOf(branchSourceInputSymbol(*branch));
        const NodeItem* from = branch->from();
        const NodeItem* to = branch->to();
        if (!from->isGround()) {
            const ss::RCP<const SymEngine::Basic> other =
                to->isGround() ? ss::RCP<const SymEngine::Basic>(integer(0)) : ss::symOf(to->acrossVariable());
            ctx.addEquationText(ctx.result.compatibilityEquations, from->acrossVariable(),
                                add(input, other));
        } else if (!to->isGround()) {
            ctx.addEquationText(ctx.result.compatibilityEquations, to->acrossVariable(), neg(input));
        }
    }
    ss::ssLog(QStringLiteral("elemental_text"),
              ctx.result.elementalEquations.join(QStringLiteral("; ")));
    ss::ssLog(QStringLiteral("continuity"), ctx.result.continuityEquations.join(QStringLiteral("; ")));
    ss::ssLog(QStringLiteral("compatibility_eqs"),
              ctx.result.compatibilityEquations.join(QStringLiteral("; ")));
    return true;
}

}  // namespace lg
