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
        result.status = StateSpaceResult::Status::NeedNormalTree;
        result.message = QStringLiteral("Find a valid normal tree first.");
        return result;
    }
    if (branches.empty()) {
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
    for (BranchItem* branch : branches) {
        if (!ss::isStateBranch(tree, branch)) {
            continue;
        }
        const bool inTree = treeSet.count(branch) != 0;
        computedStates.push_back({ss::storageStateSymbol(*branch, inTree), branch});
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
        replacements[secondary] = expr;
        addEquationText(list, secondary, expr);
    };

    for (BranchItem* branch : branches) {
        if (!branch || branch->isActive() || isTwoPortInternalBranch(*branch)) {
            continue;
        }
        const ss::RCP<const SymEngine::Basic> k = branchElementConstantExpr(*branch);
        const SystemType systemType = lg::branchSystemType(*branch);
        const BranchType type = effectivePassiveBranchType(*branch);
        const ss::RCP<const SymEngine::Basic> coeff = elementalConstantCoeff(systemType, type, k);
        const QString flowSymbol = branchFlowSymbol(*branch);
        const ss::RCP<const SymEngine::Basic> flow = ss::symOf(flowSymbol);
        const QString acrossSymbol = branchAcrossSymbol(*branch);
        const ss::RCP<const SymEngine::Basic> across = ss::symOf(acrossSymbol);

        switch (type) {
        case BranchType::A:
            elementalEquations.push_back(
                {branch, sub(flow, mul(k, ss::dotSym(acrossSymbol)))});
            addEquationText(result.elementalEquations, branch->name(), mul(k, ss::dotSym(acrossSymbol)));
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
            addEquationText(result.elementalEquations, branch->name(), mul(coeff, across));
            break;
        }
    }

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
        const ss::RCP<const SymEngine::Basic> leftAcross = ss::symOf(branchAcrossSymbol(*left));
        const ss::RCP<const SymEngine::Basic> rightAcross = ss::symOf(branchAcrossSymbol(*right));
        const ss::RCP<const SymEngine::Basic> leftFlow = ss::symOf(branchFlowSymbol(*left));
        const ss::RCP<const SymEngine::Basic> rightFlow = ss::symOf(branchFlowSymbol(*right));
        if (twoPort->kind() == TwoPortKind::Transformer) {
            elementalEquations.push_back({nullptr, sub(leftAcross, mul(k, rightAcross))});
            elementalEquations.push_back({nullptr, sub(leftFlow, div(neg(rightFlow), k))});
        } else {
            elementalEquations.push_back({nullptr, sub(leftAcross, mul(k, rightFlow))});
            elementalEquations.push_back({nullptr, sub(leftFlow, div(neg(rightAcross), k))});
        }
        result.elementalEquations.push_back(twoPort->elementalEquationText());
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

    for (BranchItem* twig : tree.treeBranches) {
        if (!twig) {
            continue;
        }
        if (twig->isActive() && twig->branchType() == BranchType::A) {
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
            if (fromIn) {
                cutSum = add(cutSum, ss::signedThrough(branch, from, to));
            } else {
                cutSum = add(cutSum, ss::signedThrough(branch, to, from));
            }
        }
        const ss::RCP<const SymEngine::Basic> twigFlow = ss::symOf(branchFlowSymbol(*twig));
        const QString twigFlowSymbol = branchFlowSymbol(*twig);
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
    }

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

    for (BranchItem* branch : tree.treeBranches) {
        if (!branch || branch->isActive()) {
            continue;
        }
        if (!usesSyntheticAcrossSymbol(*branch)) {
            continue;
        }
        const QString acrossSymbol = branchAcrossSymbol(*branch);
        const std::optional<ss::RCP<const SymEngine::Basic>> defExpr = branchAcrossExpression(*branch);
        if (!defExpr) {
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message = QStringLiteral("Could not parse across variable for %1.")
                                 .arg(branch->name());
            return result;
        }
        const SymEngine::map_basic_basic bindMap =
            ss::substitutionMap(ss::resolveReplacements(replacements));
        recordReplacement(acrossSymbol, expand(subs(*defExpr, bindMap)),
                          result.compatibilityEquations);
    }

    for (BranchItem* link : branches) {
        if (!link || treeSet.count(link) != 0) {
            continue;
        }
        if (link->isActive() && link->branchType() == BranchType::T) {
            continue;
        }
        NodeItem* u = link->from();
        NodeItem* v = link->to();
        if (!u || !v) {
            result.status = StateSpaceResult::Status::GraphError;
            result.message = QStringLiteral("A link has missing endpoints.");
            return result;
        }
        const std::optional<std::vector<BranchItem*>> path =
            ss::treePathBetween(u, v, tree.treeBranches);
        if (!path) {
            result.status = StateSpaceResult::Status::GraphError;
            result.message = QStringLiteral("No tree path found for compatibility on link %1.")
                                 .arg(link->name());
            return result;
        }
        ss::RCP<const SymEngine::Basic> loopSum = integer(0);
        NodeItem* cursor = u;
        for (BranchItem* edge : *path) {
            NodeItem* next = edge->from() == cursor ? edge->to() : edge->from();
            loopSum = add(loopSum, ss::signedAcross(edge, cursor, next));
            cursor = next;
        }
        loopSum = add(loopSum, ss::signedAcross(link, cursor, u));
        const QString linkAcrossSymbol = branchAcrossSymbol(*link);

        auto recordSyntheticAcross = [&]() -> bool {
            if (!usesSyntheticAcrossSymbol(*link)) {
                return false;
            }
            const std::optional<ss::RCP<const SymEngine::Basic>> defExpr = branchAcrossExpression(*link);
            if (!defExpr) {
                result.status = StateSpaceResult::Status::SymbolicError;
                result.message = QStringLiteral("Could not parse across variable for %1.")
                                     .arg(link->name());
                return true;
            }
            const SymEngine::map_basic_basic bindMap =
                ss::substitutionMap(ss::resolveReplacements(replacements));
            recordReplacement(linkAcrossSymbol, expand(subs(*defExpr, bindMap)),
                              result.compatibilityEquations);
            return true;
        };

        if (link->isActive() && link->branchType() == BranchType::T) {
            if (recordSyntheticAcross() &&
                result.status == StateSpaceResult::Status::SymbolicError) {
                return result;
            }
            continue;
        }

        if (recordSyntheticAcross() &&
            result.status == StateSpaceResult::Status::SymbolicError) {
            return result;
        }
        if (usesSyntheticAcrossSymbol(*link)) {
            continue;
        }

        const SymEngine::map_basic_basic bindMap =
            ss::substitutionMap(ss::resolveReplacements(replacements));
        loopSum = expand(subs(loopSum, bindMap));

        const ss::RCP<const SymEngine::Basic> linkAcross = ss::symOf(linkAcrossSymbol);
        std::optional<ss::RCP<const SymEngine::Basic>> solved = ss::solveLinearFor(loopSum, linkAcross);
        if (!solved) {
            if (eq(*expand(loopSum), *integer(0))) {
                continue;
            }
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message = QStringLiteral("Could not solve compatibility equation for %1.")
                                 .arg(linkAcrossSymbol);
            ss::ssLog(QStringLiteral("error"), result.message + QStringLiteral(" loopSum=0=") +
                                                   ss::exprText(loopSum));
            return result;
        }
        recordReplacement(linkAcrossSymbol, *solved, result.compatibilityEquations);
    }

    std::vector<QString> timedSymbols;
    timedSymbols.reserve(computedStates.size() + static_cast<size_t>(result.inputs.size()));
    for (const ComputedState& state : computedStates) {
        timedSymbols.push_back(state.symbol);
    }
    for (const QString& input : result.inputs) {
        timedSymbols.push_back(input);
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
    ss::ssLogExprs(QStringLiteral("reduced_elemental"), reducedElementals);

    std::unordered_map<QString, ss::RCP<const SymEngine::Basic>> stateDots;
    std::vector<ss::RCP<const SymEngine::Basic>> algebraics;
    for (const BranchElemental& elemental : elementalEquations) {
        const ss::RCP<const SymEngine::Basic> reduced = expand(subs(elemental.expr, valueSubMap));
        if (!elemental.branch) {
            algebraics.push_back(reduced);
            continue;
        }
        bool matched = false;
        for (const ComputedState& state : computedStates) {
            if (state.branch != elemental.branch) {
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
            ss::ssLog(QStringLiteral("matched"),
                      QStringLiteral("%1 from branch %2: 0 = %3")
                          .arg(ss::dotName(state.symbol), elemental.branch->name(),
                               ss::exprText(reduced)));
            matched = true;
            break;
        }
        if (!matched) {
            algebraics.push_back(reduced);
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
    auto allRelations = [&](const std::vector<ss::RCP<const SymEngine::Basic>>& base) {
        std::vector<ss::RCP<const SymEngine::Basic>> merged = base;
        merged.insert(merged.end(), constraintEqs.begin(), constraintEqs.end());
        return merged;
    };
    const auto dependentDotMap =
        ss::dependentDotSubstitutionMap(replacements, timedSymbols, isStateSymbol);

    std::unordered_map<QString, BranchItem*> stateBranchBySymbol;
    for (const ComputedState& state : computedStates) {
        stateBranchBySymbol[state.symbol] = state.branch;
    }
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

    ss::eliminateBranchSymbolsInto(stateDots, allRelations(algebraics), branches, treeSet,
                                   canEliminateBranchSymbol);
    ss::eliminateSymbolsInto(stateDots, allRelations(algebraics), nodeAcrossSymbols,
                             canEliminateBranchSymbol, {}, rejectTautologicalNodeElim);

    for (auto& [name, expr] : stateDots) {
        (void)name;
        expr = expand(subs(expr, valueSubMap));
        expr = expand(subs(expr, dependentDotMap));
    }
    ss::eliminateBranchSymbolsInto(stateDots, allRelations(reducedElementals), branches, treeSet,
                                   canEliminateBranchSymbol);
    ss::eliminateSymbolsInto(stateDots, allRelations(reducedElementals), nodeAcrossSymbols,
                             canEliminateBranchSymbol, {}, rejectTautologicalNodeElim);

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

    for (auto& [name, expr] : stateDots) {
        (void)name;
        expr = expand(subs(expr, dependentDotMap));
    }

    const std::vector<ss::RCP<const SymEngine::Basic>> allReductionRelations =
        allRelations(reducedElementals);
    ss::eliminateSymbolsInto(stateDots, allReductionRelations, nodeAcrossSymbols,
                             canEliminateBranchSymbol, {}, rejectTautologicalNodeElim);
    ss::eliminateBranchSymbolsInto(stateDots, allReductionRelations, branches, treeSet,
                                   canEliminateBranchSymbol);
    for (size_t pass = 0; pass < 4; ++pass) {
        bool changed = false;
        for (auto& [name, expr] : stateDots) {
            (void)name;
            const ss::RCP<const SymEngine::Basic> next =
                expand(subs(expand(subs(expr, valueSubMap)), dependentDotMap));
            if (!eq(*next, *expr)) {
                expr = next;
                changed = true;
            }
        }
        ss::eliminateBranchSymbolsInto(stateDots, allReductionRelations, branches, treeSet,
                                       canEliminateBranchSymbol);
        ss::eliminateSymbolsInto(stateDots, allReductionRelations, nodeAcrossSymbols,
                                 canEliminateBranchSymbol, {}, rejectTautologicalNodeElim);
        if (!changed) {
            break;
        }
    }
    ss::resolveStateDotCoupling(stateSymbols, stateDots);
    for (auto& [name, expr] : stateDots) {
        (void)name;
        expr = expand(subs(expr, dependentDotMap));
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
