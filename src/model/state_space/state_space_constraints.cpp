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
        if (ctx.treeSet.count(branch) != 0 && isExternalBranchOnPortSpan(*branch, branches) &&
            !isPortSpanStorageBranch(*branch, branches)) {
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
        if (twig->isActive() && twig->branchType() == BranchType::A) {
            continue;
        }
        if (skipTwigFlowContinuity(*twig, twoPorts, branches)) {
            continue;
        }
        NodeItem* root = twig->from();
        if (!root || !twig->to()) {
            ctx.result.status = StateSpaceResult::Status::GraphError;
            ctx.result.message = QStringLiteral("A tree branch has missing endpoints.");
            return false;
        }
        const std::unordered_set<NodeItem*> side =
            ss::reachableTreeNodes(root, tree.treeBranches, twig);
        ss::RCP<const SymEngine::Basic> cutSum = integer(0);
        for (BranchItem* branch : branches) {
            if (!branch || branch == twig) {
                continue;
            }
            NodeItem* from = branch->from();
            NodeItem* to = branch->to();
            if (!from || !to) {
                continue;
            }
            const bool fromIn = side.count(from) != 0;
            const bool toIn = side.count(to) != 0;
            if (fromIn == toIn) {
                continue;
            }
            if (omitFromContinuityCut(branch, branches, twoPorts)) {
                continue;
            }
            if (fromIn) {
                cutSum = add(cutSum, ss::signedThrough(branch, from, to));
            } else {
                cutSum = add(cutSum, ss::signedThrough(branch, to, from));
            }
        }
        const ss::RCP<const SymEngine::Basic> twigFlow = ss::symOf(branchThroughSymbol(*twig));
        const QString twigFlowSymbol = branchThroughSymbol(*twig);
        const std::optional<ss::RCP<const SymEngine::Basic>> solved =
            ss::solveLinearFor(add(twigFlow, cutSum), twigFlow);
        if (!solved) {
            ctx.result.status = StateSpaceResult::Status::SymbolicError;
            ctx.result.message =
                QStringLiteral("Could not solve continuity equation for %1.").arg(twig->name());
            ss::ssLog(QStringLiteral("error"), ctx.result.message);
            return false;
        }
        ctx.recordReplacement(twigFlowSymbol, *solved, ctx.result.continuityEquations);
        ss::ssLog(QStringLiteral("continuity_twig"),
                  QStringLiteral("%1 = %2").arg(twigFlowSymbol, ss::exprText(*solved)));
    }

    ss::ssLog(QStringLiteral("compatibility"));
    for (BranchItem* branch : branches) {
        if (!branch || !branch->isActive() || branch->branchType() != BranchType::A) {
            continue;
        }
        if (ctx.treeSet.count(branch) == 0) {
            continue;
        }
        NodeItem* from = branch->from();
        NodeItem* to = branch->to();
        if (!from || !to) {
            continue;
        }
        const QString input = branchSourceInputSymbol(*branch);
        const ss::RCP<const SymEngine::Basic> inputExpr = ss::symOf(input);
        if (to->isGround() && !from->isGround()) {
            const QString nodeAcross = from->acrossVariable();
            if (!nodeAcross.isEmpty() && nodeAcross != QStringLiteral("0") && nodeAcross != input) {
                ctx.recordReplacement(nodeAcross, inputExpr, ctx.result.compatibilityEquations);
            }
        } else if (from->isGround() && !to->isGround()) {
            const QString nodeAcross = to->acrossVariable();
            if (!nodeAcross.isEmpty() && nodeAcross != QStringLiteral("0") && nodeAcross != input) {
                ctx.recordReplacement(nodeAcross, inputExpr, ctx.result.compatibilityEquations);
            }
        }
    }

    const auto isPrimaryState = [&](const QString& symbolName) {
        return std::any_of(ctx.computedStates.begin(), ctx.computedStates.end(),
                           [&](const ComputedState& state) { return state.symbol == symbolName; });
    };
    ss::ssLog(QStringLiteral("port_continuity"));
    for (TwoPortItem* twoPort : twoPorts) {
        if (!twoPort) {
            continue;
        }
        for (BranchItem* port : {twoPort->leftBranch(), twoPort->rightBranch()}) {
            if (!port) {
                continue;
            }
            const QString portFlow = branchThroughSymbol(*port);
            if (portFlow.isEmpty() || isPrimaryState(portFlow) ||
                ctx.replacements.count(portFlow) != 0) {
                continue;
            }
            // ponytail: stacked transformers share one junction bond; sibling port already
            // recorded the same KCL (e.g. T4=-(T5+T_I2) vs T5=-(T4+T_I2)).
            bool siblingPortRecorded = false;
            for (BranchItem* sibling : branches) {
                if (!sibling || sibling == port || !isTwoPortInternalBranch(*sibling) ||
                    !sharesEndpoints(port, sibling)) {
                    continue;
                }
                const QString siblingFlow = branchThroughSymbol(*sibling);
                if (!siblingFlow.isEmpty() && ctx.replacements.count(siblingFlow) != 0) {
                    siblingPortRecorded = true;
                    break;
                }
            }
            if (siblingPortRecorded) {
                continue;
            }
            const ss::RCP<const SymEngine::Basic> spanSum =
                signedSpanJunctionFlowSum(*port, branches, twoPorts, true);
            if (eq(*spanSum, *integer(0))) {
                continue;
            }
            ctx.recordReplacement(portFlow, neg(spanSum), ctx.result.continuityEquations);
        }
    }
    if (!ctx.result.continuityEquations.isEmpty()) {
        ss::ssLog(QStringLiteral("continuity"), ctx.result.continuityEquations.join(QStringLiteral("; ")));
    }
    if (!ctx.result.compatibilityEquations.isEmpty()) {
        ss::ssLog(QStringLiteral("compatibility_eqs"),
                  ctx.result.compatibilityEquations.join(QStringLiteral("; ")));
    }
    ss::ssLog(QStringLiteral("two_port_across_bind"));
    auto bindTwoPortAcross = [&](const QString& dependent,
                                 const ss::RCP<const SymEngine::Basic>& expr, bool towardState = false) {
        if (dependent.isEmpty() || !isValidVariableSymbol(dependent) || isPrimaryState(dependent)) {
            return;
        }
        const SymEngine::map_basic_basic bindMap =
            ss::substitutionMap(ss::resolveReplacements(ctx.replacements));
        ctx.recordConstraint(dependent, expand(subs(expr, bindMap)), towardState);
    };
    auto bindTwoPortAcrossPair = [&](const QString& leftName,
                                     const ss::RCP<const SymEngine::Basic>& leftExpr,
                                     const QString& rightName,
                                     const ss::RCP<const SymEngine::Basic>& rightExpr) {
        if (leftName.isEmpty() || rightName.isEmpty()) {
            return;
        }
        if (isPrimaryState(leftName)) {
            bindTwoPortAcross(rightName, rightExpr, true);
        } else if (isPrimaryState(rightName)) {
            bindTwoPortAcross(leftName, leftExpr, true);
        } else if (!isPrimaryState(leftName)) {
            bindTwoPortAcross(leftName, leftExpr);
        }
    };

    for (TwoPortItem* twoPort : twoPorts) {
        if (!twoPort) {
            continue;
        }
        BranchItem* left = twoPort->leftBranch();
        BranchItem* right = twoPort->rightBranch();
        if (!left || !right) {
            continue;
        }
        const ss::RCP<const SymEngine::Basic> k = twoPortModulusExpr(*twoPort);
        const QString leftAcrossName = branchAcrossSymbol(*left);
        const QString rightAcrossName = branchAcrossSymbol(*right);
        const ss::RCP<const SymEngine::Basic> leftAcross = ss::symOf(leftAcrossName);
        const ss::RCP<const SymEngine::Basic> rightAcross = ss::symOf(rightAcrossName);
        if (twoPort->kind() == TwoPortKind::Transformer) {
            // ponytail: port flows live in elementals + continuity; across-only avoids clobbering i1=i_L etc.
            bindTwoPortAcrossPair(leftAcrossName, mul(k, rightAcross), rightAcrossName,
                                  div(leftAcross, k));
        } else {
            bindTwoPortAcrossPair(leftAcrossName, mul(k, ss::symOf(branchThroughSymbol(*right))),
                                  rightAcrossName, div(leftAcross, k));
        }
    }
    // ponytail: cascade may bind the far port first; propagate resolved across aliases toward states.
    for (size_t pass = 0; pass < twoPorts.size() + 1; ++pass) {
        bool changed = false;
        const std::unordered_map<QString, ss::RCP<const SymEngine::Basic>> resolved =
            ss::resolveReplacements(ctx.replacements);
        for (TwoPortItem* twoPort : twoPorts) {
            if (!twoPort || twoPort->kind() != TwoPortKind::Transformer) {
                continue;
            }
            BranchItem* left = twoPort->leftBranch();
            BranchItem* right = twoPort->rightBranch();
            if (!left || !right) {
                continue;
            }
            const ss::RCP<const SymEngine::Basic> k = twoPortModulusExpr(*twoPort);
            const QString leftAcrossName = branchAcrossSymbol(*left);
            const QString rightAcrossName = branchAcrossSymbol(*right);
            const auto leftIt = resolved.find(leftAcrossName);
            const auto rightIt = resolved.find(rightAcrossName);
            if (leftIt != resolved.end() && !isPrimaryState(rightAcrossName)) {
                const ss::RCP<const SymEngine::Basic> next = expand(div(leftIt->second, k));
                if (!ctx.replacements.count(rightAcrossName) ||
                    !eq(*ctx.replacements.at(rightAcrossName), *next)) {
                    bindTwoPortAcross(rightAcrossName, next, true);
                    changed = true;
                }
            } else if (rightIt != resolved.end() && !isPrimaryState(leftAcrossName)) {
                const ss::RCP<const SymEngine::Basic> next = expand(mul(rightIt->second, k));
                if (!ctx.replacements.count(leftAcrossName) ||
                    !eq(*ctx.replacements.at(leftAcrossName), *next)) {
                    bindTwoPortAcross(leftAcrossName, next, true);
                    changed = true;
                }
            }
        }
        if (!changed) {
            break;
        }
    }

    ss::ssLog(QStringLiteral("port_span_junction"));
    for (BranchItem* branch : branches) {
        if (!branch || ctx.treeSet.count(branch) == 0) {
            continue;
        }
        if (effectivePassiveBranchType(*branch) != BranchType::A) {
            continue;
        }
        if (!isPortSpanStorageBranch(*branch, branches)) {
            continue;
        }
        BranchItem* coTreePort = nullptr;
        for (TwoPortItem* twoPort : twoPorts) {
            if (!twoPort) {
                continue;
            }
            for (BranchItem* port : {twoPort->leftBranch(), twoPort->rightBranch()}) {
                if (!port || port == branch || ctx.treeSet.count(port) != 0) {
                    continue;
                }
                if (!sharesEndpoints(branch, port)) {
                    continue;
                }
                coTreePort = port;
                break;
            }
            if (coTreePort) {
                break;
            }
        }
        if (!coTreePort) {
            continue;
        }
        const std::vector<BranchItem*> parallels =
            branchesParallelToPort(coTreePort, branches, twoPorts);
        ss::RCP<const SymEngine::Basic> parallelSum = integer(0);
        for (BranchItem* parallel : parallels) {
            if (!parallel || parallel == branch) {
                continue;
            }
            parallelSum =
                add(parallelSum, signedParallelFlowExpr(*coTreePort, *parallel));
        }
        const QString storageFlowName = branchThroughSymbol(*branch);
        ss::RCP<const SymEngine::Basic> portExpr;
        bool havePortExpr = false;
        TwoPortItem* owningTwoPort = nullptr;
        for (TwoPortItem* twoPort : twoPorts) {
            if (!twoPort) {
                continue;
            }
            if (coTreePort == twoPort->leftBranch() || coTreePort == twoPort->rightBranch()) {
                owningTwoPort = twoPort;
                break;
            }
        }
        if (owningTwoPort && owningTwoPort->kind() == TwoPortKind::Transformer) {
            BranchItem* left = owningTwoPort->leftBranch();
            BranchItem* right = owningTwoPort->rightBranch();
            const ss::RCP<const SymEngine::Basic> k = twoPortModulusExpr(*owningTwoPort);
            if (coTreePort == right && left) {
                portExpr = neg(mul(k, ss::symOf(branchThroughSymbol(*left))));
                havePortExpr = true;
            } else if (coTreePort == left && right) {
                portExpr = div(neg(ss::symOf(branchThroughSymbol(*right))), k);
                havePortExpr = true;
            }
        }
        if (!havePortExpr) {
            const QString portFlowName = branchThroughSymbol(*coTreePort);
            const std::unordered_map<QString, ss::RCP<const SymEngine::Basic>> resolved =
                ss::resolveReplacements(ctx.replacements);
            const auto it = resolved.find(portFlowName);
            portExpr = it != resolved.end() ? it->second : ss::symOf(portFlowName);
        }
        const ss::RCP<const SymEngine::Basic> junctionExpr = sub(neg(portExpr), parallelSum);
        if (!eq(*junctionExpr, *ss::symOf(storageFlowName))) {
            ctx.recordReplacement(storageFlowName, junctionExpr, ctx.result.continuityEquations);
        }
    }
    return true;
}

}  // namespace lg
