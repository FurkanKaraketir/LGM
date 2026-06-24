#include "state_space.h"
#include "state_space_detail.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/subs.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lg {

namespace ss = lg::ss;

StateSpaceResult computeStateSpaceImpl(const NormalTreeResult& tree,
                                       const std::vector<NodeItem*>& nodes,
                                       const std::vector<BranchItem*>& branches,
                                       const std::vector<TwoPortItem*>& twoPorts);

StateSpaceResult computeStateSpace(const NormalTreeResult& tree,
                                   const std::vector<NodeItem*>& nodes,
                                   const std::vector<BranchItem*>& branches,
                                   const std::vector<TwoPortItem*>& twoPorts) {
    StateSpaceResult result;
    ss::ssLog(QStringLiteral("compute"),
              QStringLiteral("nodes=%1 branches=%2 two_ports=%3 tree_status=%4")
                  .arg(nodes.size())
                  .arg(branches.size())
                  .arg(twoPorts.size())
                  .arg(static_cast<int>(tree.status)));
    (void)nodes;
    try {
        return computeStateSpaceImpl(tree, nodes, branches, twoPorts);
    } catch (const std::exception& e) {
        ss::ssLog(QStringLiteral("exception"), QString::fromUtf8(e.what()));
        result.status = StateSpaceResult::Status::SymbolicError;
        result.message = QStringLiteral("Symbolic processing failed: %1")
                             .arg(QString::fromUtf8(e.what()));
        return result;
    } catch (...) {
        ss::ssLog(QStringLiteral("exception"), QStringLiteral("unknown"));
        result.status = StateSpaceResult::Status::SymbolicError;
        result.message = QStringLiteral("Symbolic processing failed.");
        return result;
    }
}

StateSpaceResult computeStateSpaceImpl(const NormalTreeResult& tree,
                                       const std::vector<NodeItem*>& nodes,
                                       const std::vector<BranchItem*>& branches,
                                       const std::vector<TwoPortItem*>& twoPorts) {
    using SymEngine::add;
    using SymEngine::div;
    using SymEngine::eq;
    using SymEngine::expand;
    using SymEngine::integer;
    using SymEngine::mul;
    using SymEngine::neg;
    using SymEngine::sub;
    using SymEngine::subs;

    StateSpaceResult result;
    if (tree.status != NormalTreeResult::Status::Ok) {
        ss::ssLog(QStringLiteral("abort"), QStringLiteral("need normal tree"));
        result.status = StateSpaceResult::Status::NeedNormalTree;
        result.message = QStringLiteral("Find a valid normal tree first.");
        return result;
    }
    if (branches.empty()) {
        ss::ssLog(QStringLiteral("abort"), QStringLiteral("no branches"));
        result.status = StateSpaceResult::Status::GraphError;
        result.message = QStringLiteral("The graph has no branches.");
        return result;
    }

    const std::unordered_set<BranchItem*> treeSet(tree.treeBranches.begin(), tree.treeBranches.end());

    for (const lg::NormalTreeResult::StateVariable& state : tree.stateVariables) {
        result.stateVariables.push_back(state.symbol);
    }

    struct ComputedState {
        QString symbol;
        BranchItem* branch = nullptr;
    };
    std::vector<ComputedState> computedStates;
    computedStates.reserve(tree.stateVariables.size());
    for (const lg::NormalTreeResult::StateVariable& treeState : tree.stateVariables) {
        BranchItem* match = nullptr;
        for (BranchItem* branch : branches) {
            if (!ss::isStateBranch(tree, branch, branches, twoPorts)) {
                continue;
            }
            const bool inTree = treeSet.count(branch) != 0;
            const QString branchSymbol =
                treeState.kind == lg::NormalTreeResult::StateVariable::Kind::Across
                    ? branchAcrossSymbol(*branch)
                    : branchThroughSymbol(*branch);
            if (branchSymbol != treeState.symbol) {
                const QString storageSymbol = ss::storageStateSymbol(*branch, inTree);
                if (storageSymbol != treeState.symbol) {
                    continue;
                }
            }
            match = branch;
            break;
        }
        if (!match) {
            ss::ssLog(QStringLiteral("abort"),
                      QStringLiteral("unmapped state %1").arg(treeState.symbol));
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message = QStringLiteral("Could not map state variable %1 to a storage branch.")
                                 .arg(treeState.symbol);
            return result;
        }
        computedStates.push_back({treeState.symbol, match});
    }

    {
        QStringList stateDesc;
        for (const ComputedState& state : computedStates) {
            const QString branchName =
                state.branch ? state.branch->name() : QStringLiteral("?");
            stateDesc.push_back(QStringLiteral("%1 (%2)").arg(state.symbol, branchName));
        }
        ss::ssLog(QStringLiteral("begin"),
                  QStringLiteral("states=[%1] tree_branches=%2")
                      .arg(stateDesc.join(QStringLiteral(", ")))
                      .arg(tree.treeBranches.size()));
    }

    std::unordered_map<QString, ss::RCP<const SymEngine::Basic>> replacements;
    std::vector<ss::RCP<const SymEngine::Basic>> constraintEquations;
    struct BranchElemental {
        BranchItem* branch = nullptr;
        ss::RCP<const SymEngine::Basic> expr;
    };
    std::vector<BranchElemental> elementalEquations;

    auto addEquationText = [](QStringList& list, const QString& lhs,
                              const ss::RCP<const SymEngine::Basic>& rhs) {
        list.push_back(QStringLiteral("%1 = %2").arg(lhs, ss::exprText(rhs)));
    };

    auto recordReplacement = [&](const QString& secondary, const ss::RCP<const SymEngine::Basic>& expr,
                                 QStringList& list) {
        if (secondary.trimmed().isEmpty()) {
            return;
        }
        if (isValidVariableSymbol(secondary)) {
            constraintEquations.push_back(
                expand(sub(ss::symOf(secondary), expr)));
        }
        replacements[secondary] = expr;
        addEquationText(list, secondary, expr);
    };

    // ponytail: compatibility display is active-A node bindings only; other constraints stay in replacements.
    auto recordConstraint = [&](const QString& secondary, const ss::RCP<const SymEngine::Basic>& expr,
                                  bool overwrite = false) {
        if (secondary.trimmed().isEmpty()) {
            return;
        }
        // ponytail: continuity/compatibility win — do not let two-port aliases overwrite tree cuts.
        if (!overwrite && replacements.count(secondary) != 0) {
            return;
        }
        if (isValidVariableSymbol(secondary)) {
            constraintEquations.push_back(
                expand(sub(ss::symOf(secondary), expr)));
        }
        replacements[secondary] = expr;
    };

    std::vector<QString> timedSymbols = ss::collectNodeAcrossSymbols(nodes);
    auto appendTimed = [&](const QString& symbol) {
        if (symbol.isEmpty() || !isValidVariableSymbol(symbol)) {
            return;
        }
        if (std::find(timedSymbols.begin(), timedSymbols.end(), symbol) == timedSymbols.end()) {
            timedSymbols.push_back(symbol);
        }
    };
    for (const ComputedState& state : computedStates) {
        appendTimed(state.symbol);
    }
    for (BranchItem* branch : branches) {
        if (!branch || !branch->isActive()) {
            continue;
        }
        appendTimed(branchSourceInputSymbol(*branch));
    }

    ss::ssLog(QStringLiteral("build_elementals"),
              QStringLiteral("passive_branches=%1").arg(branches.size()));
    for (BranchItem* branch : branches) {
        if (!branch || branch->isActive() || isTwoPortInternalBranch(*branch)) {
            continue;
        }
        if (treeSet.count(branch) != 0 && isExternalBranchOnPortSpan(*branch, branches) &&
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
            ss::linearTimeDerivative(across, timedSymbols);

        switch (type) {
        case BranchType::A:
            elementalEquations.push_back({branch, sub(flow, mul(coeff, acrossDot))});
            addEquationText(result.elementalEquations, flowSymbol, mul(k, acrossDot));
            break;
        case BranchType::T: {
            const ss::RCP<const SymEngine::Basic> displayCoeff =
                elementalConstantInNumerator(systemType, BranchType::T) ? div(integer(1), k) : k;
            elementalEquations.push_back(
                {branch, sub(ss::dotSym(flowSymbol), mul(coeff, across))});
            addEquationText(result.elementalEquations, branchAcrossVariableText(*branch),
                            mul(displayCoeff, ss::dotSym(flowSymbol)));
            break;
        }
        case BranchType::D:
            elementalEquations.push_back({branch, sub(flow, mul(coeff, across))});
            addEquationText(result.elementalEquations, flowSymbol, mul(coeff, across));
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
            result.status = StateSpaceResult::Status::GraphError;
            result.message = QStringLiteral("A two-port element has missing branches.");
            return result;
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
            elementalEquations.push_back({nullptr, sub(leftAcross, mul(k, rightAcross))});
            elementalEquations.push_back({nullptr, sub(leftThrough, div(neg(rightThrough), k))});
            result.elementalEquations.push_back(
                QStringLiteral("%1 = %2; %3 = %4")
                    .arg(leftAcrossName, ss::exprText(mul(k, rightAcross)), leftThroughName,
                         ss::exprText(div(neg(rightThrough), k))));
        } else {
            elementalEquations.push_back({nullptr, sub(leftAcross, mul(k, rightThrough))});
            elementalEquations.push_back({nullptr, sub(leftThrough, div(neg(rightAcross), k))});
            result.elementalEquations.push_back(twoPort->elementalEquationText());
        }
    }

    for (BranchItem* branch : branches) {
        if (!branch || !branch->isActive()) {
            continue;
        }
        const bool inTree = treeSet.count(branch) != 0;
        if (branch->branchType() == BranchType::A && inTree) {
            const QString symbol = branchSourceInputSymbol(*branch);
            result.inputs.push_back(symbol);
            result.inputLabels.push_back(branchSourceInputDisplay(*branch));
        } else if (branch->branchType() == BranchType::T && !inTree) {
            const QString symbol = branchSourceInputSymbol(*branch);
            result.inputs.push_back(symbol);
            result.inputLabels.push_back(branchSourceInputDisplay(*branch));
        }
    }

    if (!result.inputs.isEmpty()) {
        ss::ssLog(QStringLiteral("inputs"), result.inputs.join(QStringLiteral(", ")));
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
            result.status = StateSpaceResult::Status::GraphError;
            result.message = QStringLiteral("A tree branch has missing endpoints.");
            return result;
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
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message =
                QStringLiteral("Could not solve continuity equation for %1.").arg(twig->name());
            ss::ssLog(QStringLiteral("error"), result.message);
            return result;
        }
        recordReplacement(twigFlowSymbol, *solved, result.continuityEquations);
        ss::ssLog(QStringLiteral("continuity_twig"),
                  QStringLiteral("%1 = %2").arg(twigFlowSymbol, ss::exprText(*solved)));
    }

    ss::ssLog(QStringLiteral("compatibility"));
    for (BranchItem* branch : branches) {
        if (!branch || !branch->isActive() || branch->branchType() != BranchType::A) {
            continue;
        }
        if (treeSet.count(branch) == 0) {
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
                recordReplacement(nodeAcross, inputExpr, result.compatibilityEquations);
            }
        } else if (from->isGround() && !to->isGround()) {
            const QString nodeAcross = to->acrossVariable();
            if (!nodeAcross.isEmpty() && nodeAcross != QStringLiteral("0") && nodeAcross != input) {
                recordReplacement(nodeAcross, inputExpr, result.compatibilityEquations);
            }
        }
    }

    const auto isPrimaryState = [&](const QString& symbolName) {
        return std::any_of(computedStates.begin(), computedStates.end(),
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
                replacements.count(portFlow) != 0) {
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
                if (!siblingFlow.isEmpty() && replacements.count(siblingFlow) != 0) {
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
            recordReplacement(portFlow, neg(spanSum), result.continuityEquations);
        }
    }
    if (!result.continuityEquations.isEmpty()) {
        ss::ssLog(QStringLiteral("continuity"), result.continuityEquations.join(QStringLiteral("; ")));
    }
    if (!result.compatibilityEquations.isEmpty()) {
        ss::ssLog(QStringLiteral("compatibility_eqs"),
                  result.compatibilityEquations.join(QStringLiteral("; ")));
    }
    ss::ssLog(QStringLiteral("two_port_across_bind"));
    auto bindTwoPortAcross = [&](const QString& dependent,
                                 const ss::RCP<const SymEngine::Basic>& expr, bool towardState = false) {
        if (dependent.isEmpty() || !isValidVariableSymbol(dependent) || isPrimaryState(dependent)) {
            return;
        }
        const SymEngine::map_basic_basic bindMap =
            ss::substitutionMap(ss::resolveReplacements(replacements));
        recordConstraint(dependent, expand(subs(expr, bindMap)), towardState);
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
            ss::resolveReplacements(replacements);
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
                if (!replacements.count(rightAcrossName) ||
                    !eq(*replacements.at(rightAcrossName), *next)) {
                    bindTwoPortAcross(rightAcrossName, next, true);
                    changed = true;
                }
            } else if (rightIt != resolved.end() && !isPrimaryState(leftAcrossName)) {
                const ss::RCP<const SymEngine::Basic> next = expand(mul(rightIt->second, k));
                if (!replacements.count(leftAcrossName) ||
                    !eq(*replacements.at(leftAcrossName), *next)) {
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
        if (!branch || treeSet.count(branch) == 0) {
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
                if (!port || port == branch || treeSet.count(port) != 0) {
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
                ss::resolveReplacements(replacements);
            const auto it = resolved.find(portFlowName);
            portExpr = it != resolved.end() ? it->second : ss::symOf(portFlowName);
        }
        const ss::RCP<const SymEngine::Basic> junctionExpr = sub(neg(portExpr), parallelSum);
        if (!eq(*junctionExpr, *ss::symOf(storageFlowName))) {
            recordReplacement(storageFlowName, junctionExpr, result.continuityEquations);
        }
    }

  ss::ssLog(QStringLiteral("mass_spring_reflect"));
  // ponytail: LGM mass-in-tree + co-tree T-type storage — TF chain reflects link through into f_M
  {
        BranchItem* massBranch = nullptr;
        for (BranchItem* branch : branches) {
            if (!branch || treeSet.count(branch) == 0) {
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
            const NodeItem* massEffort = nullptr;
            if (massBranch->from() && !massBranch->from()->isGround()) {
                massEffort = massBranch->from();
            } else if (massBranch->to() && !massBranch->to()->isGround()) {
                massEffort = massBranch->to();
            }
            const ComputedState* complianceState = nullptr;
            for (const ComputedState& state : computedStates) {
                if (!state.branch || treeSet.count(state.branch) != 0) {
                    continue;
                }
                if (effectivePassiveBranchType(*state.branch) != BranchType::T) {
                    continue;
                }
                complianceState = &state;
                break;
            }
            const NodeItem* springEffort = nullptr;
            if (complianceState && complianceState->branch) {
                if (complianceState->branch->from() &&
                    !complianceState->branch->from()->isGround()) {
                    springEffort = complianceState->branch->from();
                } else if (complianceState->branch->to() &&
                           !complianceState->branch->to()->isGround()) {
                    springEffort = complianceState->branch->to();
                }
            }
            if (massEffort && springEffort && complianceState) {
                const std::optional<ss::RCP<const SymEngine::Basic>> tfProduct =
                    ss::transformerModulusProductBetween(massEffort, springEffort, twoPorts);
                if (tfProduct) {
                    ss::RCP<const SymEngine::Basic> inputSum = integer(0);
                    for (BranchItem* branch : branches) {
                        if (!branch || !branch->isActive() ||
                            branch->branchType() != BranchType::T) {
                            continue;
                        }
                        if (branch->from() != massEffort && branch->to() != massEffort) {
                            continue;
                        }
                        inputSum = add(inputSum, ss::symOf(branchSourceInputSymbol(*branch)));
                    }
                    if (eq(*inputSum, *integer(0)) && !result.inputs.empty()) {
                        inputSum = ss::symOf(result.inputs.front());
                    }
                    if (!eq(*inputSum, *integer(0))) {
                        const ss::RCP<const SymEngine::Basic> reflected =
                            div(ss::symOf(complianceState->symbol), *tfProduct);
                        const QString massFlow = branchThroughSymbol(*massBranch);
                        recordReplacement(massFlow, sub(inputSum, reflected),
                                          result.continuityEquations);
                    }
                }
            }
        }
    }

    for (const QString& input : result.inputs) {
        appendTimed(input);
    }
    {
        QStringList timedList;
        for (const QString& s : timedSymbols) {
            timedList.push_back(s);
        }
        ss::ssLog(QStringLiteral("timed_symbols"), timedList.join(QStringLiteral(", ")));
    }

    ss::ssLog(QStringLiteral("substitute_elementals"));
    std::vector<ss::RCP<const SymEngine::Basic>> rawElementals;
    rawElementals.reserve(elementalEquations.size());
    for (const BranchElemental& elemental : elementalEquations) {
        rawElementals.push_back(expand(elemental.expr));
    }

    const SymEngine::map_basic_basic valueSubMap =
        ss::substitutionMap(ss::resolveReplacements(replacements));
    const SymEngine::map_basic_basic subMap = ss::extendedSubstitutionMap(replacements, timedSymbols);
    std::vector<ss::RCP<const SymEngine::Basic>> reducedElementals;
    reducedElementals.reserve(elementalEquations.size());
    for (const BranchElemental& elemental : elementalEquations) {
        reducedElementals.push_back(expand(subs(elemental.expr, subMap)));
    }

    ss::ssLog(QStringLiteral("replacements"), QString::number(replacements.size()));
    {
        QStringList replLines;
        for (const auto& [name, expr] : ss::resolveReplacements(replacements)) {
            replLines.push_back(QStringLiteral("%1 = %2").arg(name, ss::exprText(expr)));
        }
        ss::ssLog(QStringLiteral("replacements_resolved"), replLines.join(QStringLiteral("; ")));
    }
    if (!result.elementalEquations.isEmpty()) {
        ss::ssLog(QStringLiteral("elemental_text"), result.elementalEquations.join(QStringLiteral("; ")));
    }
    ss::ssLogExprs(QStringLiteral("elemental"), rawElementals);
    ss::ssLogExprs(QStringLiteral("reduced_elemental"), reducedElementals);

    ss::ssLog(QStringLiteral("match_state_dots"));
    std::unordered_map<QString, ss::RCP<const SymEngine::Basic>> stateDots;
    std::unordered_set<BranchItem*> stateMatchedBranches;
    std::vector<ss::RCP<const SymEngine::Basic>> algebraics;
    std::vector<ss::RCP<const SymEngine::Basic>> matchedAlgebraics;
    for (const BranchElemental& elemental : elementalEquations) {
        const ss::RCP<const SymEngine::Basic> reduced = expand(subs(elemental.expr, valueSubMap));
        if (!elemental.branch) {
            algebraics.push_back(reduced);
            continue;
        }
        bool matched = false;
        const QString elementalStateSymbol =
            elemental.branch
                ? ss::storageStateSymbol(*elemental.branch,
                                         treeSet.count(elemental.branch) != 0)
                : QString();
        for (const ComputedState& state : computedStates) {
            if (state.branch != elemental.branch &&
                (elementalStateSymbol.isEmpty() || state.symbol != elementalStateSymbol)) {
                continue;
            }
            const ss::RCP<const SymEngine::Basic> dot = ss::dotSym(state.symbol);
            const std::optional<ss::RCP<const SymEngine::Basic>> solved =
                ss::solveLinearFor(reduced, dot);
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

    if (stateDots.size() != computedStates.size()) {
        QStringList missing;
        for (const ComputedState& state : computedStates) {
            if (stateDots.count(state.symbol) == 0) {
                const QString branchName =
                    state.branch ? state.branch->name() : QStringLiteral("?");
                missing.push_back(QStringLiteral("%1 (%2)").arg(state.symbol, branchName));
            }
        }
        ss::ssLog(QStringLiteral("error"),
                  QStringLiteral("missing state dots: %1").arg(missing.join(QStringLiteral(", "))));
        ss::ssLogExprs(QStringLiteral("algebraic"), algebraics);
        result.status = StateSpaceResult::Status::SymbolicError;
        result.message = QStringLiteral("Could not derive all state equations.");
        return result;
    }

    for (const ComputedState& state : computedStates) {
        ss::ssLog(QStringLiteral("state_dot_initial"),
                  QStringLiteral("%1 = %2")
                      .arg(ss::dotName(state.symbol), ss::exprText(stateDots.at(state.symbol))));
    }

    const auto isStateSymbol = [&](const QString& symbolName) {
        for (const ComputedState& state : computedStates) {
            if (state.symbol == symbolName) {
                return true;
            }
        }
        return false;
    };
    const auto isInputLike = [&](const QString& symbolName) {
        if (std::find(result.inputs.begin(), result.inputs.end(), symbolName) != result.inputs.end()) {
            return true;
        }
        for (const QString& input : result.inputs) {
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
        ss::constraintRelations(replacements);
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
        for (size_t i = 0; i < elementalEquations.size(); ++i) {
            BranchItem* branch = elementalEquations[i].branch;
            if (branch && stateMatchedBranches.count(branch) != 0) {
                continue;
            }
            merged.push_back(reducedElementals[i]);
        }
        merged.insert(merged.end(), constraintEqs.begin(), constraintEqs.end());
        return merged;
    };
    const auto dependentDotMap =
        ss::dependentDotSubstitutionMap(replacements, timedSymbols, isStateSymbol);

    const std::vector<ss::RCP<const SymEngine::Basic>> stateBranchFlowRelations = [&]() {
        std::vector<ss::RCP<const SymEngine::Basic>> relations;
        for (const ComputedState& state : computedStates) {
            if (!state.branch || state.branch->isActive() ||
                isTwoPortInternalBranch(*state.branch)) {
                continue;
            }
            const bool inTree = treeSet.count(state.branch) != 0;
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
    for (const ComputedState& state : computedStates) {
        stateBranchBySymbol[state.symbol] = state.branch;
    }

    const std::vector<ss::RCP<const SymEngine::Basic>> inputPathRelations =
        withFlowRelations(allRelations(algebraics));
    std::unordered_set<QString> inputCarriers;
    if (!result.inputs.isEmpty()) {
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
            for (const QString& input : result.inputs) {
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
        for (const QString& input : result.inputs) {
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
            if (!across.isEmpty() &&
                !eq(*ss::linearCoeffRCP(solved, ss::symOf(across)), *integer(0))) {
                return false;
            }
        }
        return !ss::substitutionIsCircular(targetExpr, primarySym, solved);
    };
    const auto rejectPortAliasInverse = [&](const ss::RCP<const SymEngine::Basic>& primarySym,
                                            const ss::RCP<const SymEngine::Basic>& solved) {
        for (const auto& [portFlow, externalFlow] : replacements) {
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
        for (const QString& input : result.inputs) {
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
        for (const QString& input : result.inputs) {
            if (!eq(*ss::linearCoeffRCP(next, ss::symOf(input)), *integer(0))) {
                return true;
            }
            if (!eq(*ss::linearCoeffRCP(next, ss::symOf(ss::dotName(input))), *integer(0))) {
                return true;
            }
        }
        std::vector<ss::RCP<const SymEngine::Basic>> otherStates;
        otherStates.reserve(computedStates.size());
        for (const ComputedState& state : computedStates) {
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
                ss::resolveReplacements(replacements);
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
                                   treeSet, canEliminateBranchSymbol, acceptStateDotSubstitution,
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
    for (const ComputedState& state : computedStates) {
        ss::ssLog(QStringLiteral("state_dot_after_value_sub"),
                  QStringLiteral("%1 = %2")
                      .arg(ss::dotName(state.symbol), ss::exprText(stateDots.at(state.symbol))));
    }
    ss::eliminateBranchSymbolsInto(stateDots, withFlowRelations(stateDotReducedRelations()),
                                   branches, treeSet, canEliminateBranchSymbol,
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
    stateSymbols.reserve(computedStates.size());
    for (const ComputedState& state : computedStates) {
        stateSymbols.push_back(state.symbol);
    }

    ss::ssLog(QStringLiteral("pre_coupling"), QStringLiteral("algebraics=%1").arg(algebraics.size()));
    for (const ComputedState& state : computedStates) {
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
    ss::eliminateBranchSymbolsInto(stateDots, postCouplingRelations, branches, treeSet,
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
        ss::eliminateBranchSymbolsInto(stateDots, allReductionRelations, branches, treeSet,
                                       canEliminateBranchSymbol, acceptStateDotSubstitution,
                                       QStringLiteral("elim_branch_refine_%1").arg(pass));
        ss::eliminateSymbolsInto(stateDots, allReductionRelations, nodeAcrossSymbols,
                                 canEliminateBranchSymbol, {}, acceptStateDotSubstitution,
                                 QStringLiteral("elim_node_refine_%1").arg(pass));
        ss::eliminateBranchSymbolsInto(stateDots, postCouplingRelations, branches, treeSet,
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
    ss::eliminateBranchSymbolsInto(stateDots, postCouplingRelations, branches, treeSet,
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
    stateInputVars.reserve(stateSymbols.size() + 2 * static_cast<size_t>(result.inputs.size()));
    for (const QString& symbolName : stateSymbols) {
        stateInputVars.push_back(ss::symOf(symbolName));
    }
    for (const QString& input : result.inputs) {
        stateInputVars.push_back(ss::symOf(input));
        stateInputVars.push_back(ss::symOf(ss::dotName(input)));
    }
    for (const ComputedState& state : computedStates) {
        const ss::RCP<const SymEngine::Basic> remainder =
            ss::linearRemainder(stateDots.at(state.symbol), stateInputVars);
        if (!eq(*remainder, *integer(0))) {
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message = QStringLiteral("State equation for %1 has unresolved terms: %2.")
                                 .arg(ss::dotName(state.symbol), ss::exprText(remainder));
            ss::ssLog(QStringLiteral("error"), result.message);
            ss::ssLog(QStringLiteral("state_dot_full"),
                      QStringLiteral("%1 = %2")
                          .arg(ss::dotName(state.symbol), ss::exprText(stateDots.at(state.symbol))));
            return result;
        }
    }

    ss::ssLog(QStringLiteral("ok"), QStringLiteral("order=%1").arg(computedStates.size()));

    for (const ComputedState& state : computedStates) {
        const QString lhs = ss::dotName(state.symbol);
        const QString rhs = ss::displayExprText(stateDots.at(state.symbol), result.inputs,
                                                result.inputLabels);
        result.stateEquations.push_back(QStringLiteral("%1 = %2").arg(lhs, rhs));
        ss::ssLog(QStringLiteral("state_equation"), QStringLiteral("%1 = %2").arg(lhs, rhs));
    }

    if (!computedStates.empty()) {
        QStringList xDotEntries;
        QStringList xEntries;
        std::vector<QStringList> aRows;
        std::vector<QStringList> bRows;
        std::vector<QStringList> eRows;
        QStringList uEntries;
        QStringList uDotEntries;
        xDotEntries.reserve(static_cast<int>(computedStates.size()));
        xEntries.reserve(static_cast<int>(computedStates.size()));
        aRows.reserve(computedStates.size());
        for (int i = 0; i < result.inputs.size(); ++i) {
            const QString& input = result.inputs.at(i);
            uEntries.push_back(ss::latexMathSymbol(
                i < result.inputLabels.size() ? result.inputLabels.at(i) : input));
            uDotEntries.push_back(i < result.inputLabels.size()
                                      ? ss::latexInputDotLabel(result.inputLabels.at(i))
                                      : ss::latexMathSymbol(ss::dotName(input)));
        }
        if (!result.inputs.isEmpty()) {
            bRows.reserve(computedStates.size());
            eRows.reserve(computedStates.size());
        }
        for (const ComputedState& state : computedStates) {
            const ss::RCP<const SymEngine::Basic> rhs = stateDots.at(state.symbol);
            xDotEntries.push_back(ss::latexMathSymbol(ss::dotName(state.symbol)));
            xEntries.push_back(ss::latexMathSymbol(state.symbol));
            QStringList aRow;
            aRow.reserve(static_cast<int>(computedStates.size()));
            for (const ComputedState& col : computedStates) {
                aRow.push_back(ss::latexCoeff(ss::linearCoeffRCP(rhs, ss::symOf(col.symbol))));
            }
            aRows.push_back(aRow);
            if (!result.inputs.isEmpty()) {
                QStringList bRow;
                QStringList eRow;
                bRow.reserve(result.inputs.size());
                eRow.reserve(result.inputs.size());
                for (const QString& input : result.inputs) {
                    bRow.push_back(ss::latexCoeff(ss::linearCoeffRCP(rhs, ss::symOf(input))));
                    eRow.push_back(ss::latexCoeff(ss::linearCoeffRCP(rhs, ss::symOf(ss::dotName(input)))));
                }
                bRows.push_back(bRow);
                eRows.push_back(eRow);
            }
        }
        QString latex = QStringLiteral("$");
        latex += ss::latexColumnVector(xDotEntries);
        latex += QStringLiteral(" = ");
        latex += ss::latexBmatrix(aRows);
        latex += QStringLiteral(" ");
        latex += ss::latexColumnVector(xEntries);
        if (!result.inputs.isEmpty()) {
            latex += QStringLiteral(" + ");
            latex += ss::latexBmatrix(bRows);
            latex += QStringLiteral(" ");
            latex += ss::latexColumnVector(uEntries);
            if (!ss::matrixIsZero(eRows)) {
                latex += QStringLiteral(" + ");
                latex += ss::latexBmatrix(eRows);
                latex += QStringLiteral(" ");
                latex += ss::latexColumnVector(uDotEntries);
            }
        }
        latex += QStringLiteral("$");
        result.matrixForm = latex;
        ss::ssLog(QStringLiteral("matrix_form"), latex);
    }

    result.status = StateSpaceResult::Status::Ok;
    result.message = QStringLiteral("State order %1, %2 continuity, %3 compatibility, %4 inputs.")
                         .arg(computedStates.size())
                         .arg(result.continuityEquations.size())
                         .arg(result.compatibilityEquations.size())
                         .arg(result.inputs.size());
    return result;
}

}  // namespace lg
