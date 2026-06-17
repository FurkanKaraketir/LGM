#include "state_space.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <symengine/basic.h>
#include <symengine/integer.h>
#include <symengine/add.h>
#include <symengine/mul.h>
#include <symengine/parser.h>
#include <symengine/printers/strprinter.h>
#include <symengine/subs.h>
#include <symengine/symbol.h>

#include <algorithm>
#include <cassert>
#include <optional>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <QRegularExpression>

namespace lg {

namespace {

using SymEngine::Basic;
using SymEngine::RCP;
using SymEngine::add;
using SymEngine::div;
using SymEngine::eq;
using SymEngine::expand;
using SymEngine::integer;
using SymEngine::mul;
using SymEngine::neg;
using SymEngine::parse;
using SymEngine::sub;
using SymEngine::subs;
using SymEngine::symbol;

std::optional<RCP<const Basic>> trySymOf(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == QStringLiteral("0")) {
        return integer(0);
    }
    if (!isValidVariableSymbol(trimmed)) {
        return std::nullopt;
    }
    try {
        return parse(trimmed.toStdString());
    } catch (...) {
        return std::nullopt;
    }
}

RCP<const Basic> symOf(const QString& name) {
    const std::optional<RCP<const Basic>> sym = trySymOf(name);
    if (!sym) {
        throw std::runtime_error("invalid symbol");
    }
    return *sym;
}

QString exprText(const RCP<const Basic>& expr) {
    return QString::fromStdString(SymEngine::str(*expr));
}

QString dotName(const QString& base) {
    return base + QStringLiteral("_dot");
}

RCP<const Basic> dotSym(const QString& base) { return symOf(dotName(base)); }

QString storageStateSymbol(const BranchItem& branch, bool inTree) {
    if (branch.branchType() == BranchType::A && inTree) {
        return branchAcrossSymbol(branch);
    }
    return branchFlowSymbol(branch);
}

bool isStateBranch(const NormalTreeResult& tree, BranchItem* branch) {
    if (!branch || branch->isActive()) {
        return false;
    }
    const bool inTree =
        std::find(tree.treeBranches.begin(), tree.treeBranches.end(), branch) !=
        tree.treeBranches.end();
    if (branch->branchType() == BranchType::A && inTree) {
        return true;
    }
    if (branch->branchType() == BranchType::T && !inTree) {
        return true;
    }
    return false;
}

std::unordered_set<NodeItem*> reachableTreeNodes(NodeItem* start,
                                                  const std::vector<BranchItem*>& treeBranches,
                                                  BranchItem* excludedTwig) {
    std::unordered_set<NodeItem*> seen;
    if (!start) {
        return seen;
    }
    std::queue<NodeItem*> queue;
    seen.insert(start);
    queue.push(start);
    while (!queue.empty()) {
        NodeItem* node = queue.front();
        queue.pop();
        for (BranchItem* branch : node->branches()) {
            if (!branch || branch == excludedTwig) {
                continue;
            }
            if (std::find(treeBranches.begin(), treeBranches.end(), branch) ==
                treeBranches.end()) {
                continue;
            }
            NodeItem* other = branch->from() == node ? branch->to() : branch->from();
            if (!other || seen.count(other) != 0) {
                continue;
            }
            seen.insert(other);
            queue.push(other);
        }
    }
    return seen;
}

std::optional<std::vector<BranchItem*>> treePathBetween(NodeItem* start, NodeItem* goal,
                                                        const std::vector<BranchItem*>& treeBranches) {
    if (!start || !goal) {
        return std::nullopt;
    }
    if (start == goal) {
        return std::vector<BranchItem*>{};
    }
    std::unordered_map<NodeItem*, BranchItem*> parentEdge;
    std::queue<NodeItem*> queue;
    parentEdge[start] = nullptr;
    queue.push(start);
    while (!queue.empty()) {
        NodeItem* node = queue.front();
        queue.pop();
        for (BranchItem* branch : node->branches()) {
            if (!branch) {
                continue;
            }
            if (std::find(treeBranches.begin(), treeBranches.end(), branch) == treeBranches.end()) {
                continue;
            }
            NodeItem* other = branch->from() == node ? branch->to() : branch->from();
            if (!other || parentEdge.count(other) != 0) {
                continue;
            }
            parentEdge[other] = branch;
            if (other == goal) {
                std::vector<BranchItem*> path;
                for (NodeItem* at = goal; at != start;) {
                    BranchItem* edge = parentEdge[at];
                    if (!edge) {
                        return std::nullopt;
                    }
                    path.push_back(edge);
                    at = edge->from() == at ? edge->to() : edge->from();
                }
                std::reverse(path.begin(), path.end());
                return path;
            }
            queue.push(other);
        }
    }
    return std::nullopt;
}

RCP<const Basic> signedAcross(BranchItem* branch, NodeItem* from, NodeItem* to) {
    if (!branch || !from || !to) {
        return integer(0);
    }
    const RCP<const Basic> across = symOf(branchAcrossSymbol(*branch));
    if (branch->from() == from && branch->to() == to) {
        return across;
    }
    if (branch->from() == to && branch->to() == from) {
        return neg(across);
    }
    return integer(0);
}

RCP<const Basic> signedThrough(BranchItem* branch, NodeItem* from, NodeItem* to) {
    if (!branch || !from || !to) {
        return integer(0);
    }
    const RCP<const Basic> flow = symOf(branchFlowSymbol(*branch));
    if (branch->from() == from && branch->to() == to) {
        return flow;
    }
    if (branch->from() == to && branch->to() == from) {
        return neg(flow);
    }
    return integer(0);
}

std::optional<RCP<const Basic>> solveLinearFor(const RCP<const Basic>& equation,
                                               const RCP<const Basic>& unknown) {
    const RCP<const Basic> residual = expand(sub(equation, integer(0)));
    const RCP<const Basic> withVar = subs(residual, {{unknown, integer(1)}});
    const RCP<const Basic> withoutVar = subs(residual, {{unknown, integer(0)}});
    const RCP<const Basic> coeff = expand(sub(withVar, withoutVar));
    if (eq(*coeff, *integer(0))) {
        return std::nullopt;
    }
    return div(neg(withoutVar), coeff);
}

SymEngine::map_basic_basic substitutionMap(
    const std::unordered_map<QString, RCP<const Basic>>& replacements) {
    SymEngine::map_basic_basic out;
    for (const auto& [name, expr] : replacements) {
        out[symOf(name)] = expr;
    }
    return out;
}

QString linearCoeff(const RCP<const Basic>& expr, const RCP<const Basic>& variable) {
    const RCP<const Basic> expanded = expand(expr);
    const RCP<const Basic> withVar = subs(expanded, {{variable, integer(1)}});
    const RCP<const Basic> withoutVar = subs(expanded, {{variable, integer(0)}});
    return exprText(sub(withVar, withoutVar));
}

QString latexMathSymbol(const QString& name) {
    if (name.endsWith(QStringLiteral("_dot"))) {
        return QStringLiteral("\\dot{%1}").arg(latexMathSymbol(name.left(name.size() - 4)));
    }
    return name;
}

QString latexCoeffExpr(const QString& symEngineText) {
    if (symEngineText.isEmpty() || symEngineText == QStringLiteral("0")) {
        return QStringLiteral("0");
    }
    QString s = symEngineText.trimmed();

    s.replace(QRegularExpression(QStringLiteral(R"(\*\*\((-?\d+)\))")), QStringLiteral("^{\\1}"));
    s.replace(QRegularExpression(QStringLiteral(R"(\*\*(\d+))")), QStringLiteral("^{\\1}"));

    const QRegularExpression invRe(QStringLiteral(R"(^([A-Za-z0-9_]+)\^\{-1\}$)"));
    const QRegularExpressionMatch invMatch = invRe.match(s);
    if (invMatch.hasMatch()) {
        return QStringLiteral("\\frac{1}{%1}").arg(invMatch.captured(1));
    }

    const int slash = s.indexOf(QLatin1Char('/'));
    if (slash > 0 && !s.contains(QLatin1Char('*'))) {
        return QStringLiteral("\\frac{%1}{%2}")
            .arg(latexCoeffExpr(s.left(slash)), latexCoeffExpr(s.mid(slash + 1)));
    }

    if (s.contains(QLatin1Char('*'))) {
        const QStringList parts = s.split(QLatin1Char('*'), Qt::SkipEmptyParts);
        QStringList latexParts;
        latexParts.reserve(parts.size());
        for (const QString& part : parts) {
            latexParts.push_back(latexCoeffExpr(part));
        }
        return latexParts.join(QStringLiteral(" \\cdot "));
    }

    return s;
}

QString latexBmatrix(const std::vector<QStringList>& rows) {
    QStringList latexRows;
    for (const QStringList& row : rows) {
        latexRows.push_back(row.join(QStringLiteral(" & ")));
    }
    return QStringLiteral("\\begin{bmatrix}%1\\end{bmatrix}")
        .arg(latexRows.join(QStringLiteral(" \\\\ ")));
}

QString latexColumnVector(const QStringList& entries) {
    std::vector<QStringList> rows;
    rows.reserve(static_cast<size_t>(entries.size()));
    for (const QString& entry : entries) {
        rows.push_back({entry});
    }
    return latexBmatrix(rows);
}

}  // namespace

StateSpaceResult computeStateSpaceImpl(const NormalTreeResult& tree,
                                       const std::vector<BranchItem*>& branches,
                                       const std::vector<TwoPortItem*>& twoPorts);

StateSpaceResult computeStateSpace(const NormalTreeResult& tree,
                                   const std::vector<NodeItem*>& nodes,
                                   const std::vector<BranchItem*>& branches,
                                   const std::vector<TwoPortItem*>& twoPorts) {
    StateSpaceResult result;
    (void)nodes;
    try {
        return computeStateSpaceImpl(tree, branches, twoPorts);
    } catch (const std::exception&) {
        result.status = StateSpaceResult::Status::SymbolicError;
        result.message = QStringLiteral("Symbolic processing failed.");
        return result;
    } catch (...) {
        result.status = StateSpaceResult::Status::SymbolicError;
        result.message = QStringLiteral("Symbolic processing failed.");
        return result;
    }
}

StateSpaceResult computeStateSpaceImpl(const NormalTreeResult& tree,
                                       const std::vector<BranchItem*>& branches,
                                       const std::vector<TwoPortItem*>& twoPorts) {
    StateSpaceResult result;
    if (tree.status != NormalTreeResult::Status::Ok) {
        result.status = StateSpaceResult::Status::NeedNormalTree;
        result.message = QStringLiteral("Find a valid normal tree first.");
        return result;
    }
    if (!twoPorts.empty()) {
        result.status = StateSpaceResult::Status::Unsupported;
        result.message = QStringLiteral("State-space generation for two-port graphs is not supported yet.");
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
        if (!isStateBranch(tree, branch)) {
            continue;
        }
        const bool inTree = treeSet.count(branch) != 0;
        computedStates.push_back({storageStateSymbol(*branch, inTree), branch});
    }

    std::unordered_map<QString, RCP<const Basic>> replacements;
    std::vector<RCP<const Basic>> elementalEquations;

    auto addEquationText = [](QStringList& list, const QString& lhs, const RCP<const Basic>& rhs) {
        list.push_back(QStringLiteral("%1 = %2").arg(lhs, exprText(rhs)));
    };

    auto recordReplacement = [&](const QString& secondary, const RCP<const Basic>& expr,
                                 QStringList& list) {
        if (secondary.trimmed().isEmpty()) {
            return;
        }
        replacements[secondary] = expr;
        addEquationText(list, secondary, expr);
    };

    for (BranchItem* branch : branches) {
        if (!branch || branch->isActive()) {
            continue;
        }
        const bool inTree = treeSet.count(branch) != 0;
        const RCP<const Basic> k = branchElementConstantExpr(*branch);
        const QString flowSymbol = branchFlowSymbol(*branch);
        const RCP<const Basic> flow = symOf(flowSymbol);
        const QString acrossSymbol = branchAcrossSymbol(*branch);
        const RCP<const Basic> across = symOf(acrossSymbol);

        switch (branch->branchType()) {
        case BranchType::A:
            if (inTree) {
                elementalEquations.push_back(sub(flow, mul(k, dotSym(acrossSymbol))));
                addEquationText(result.elementalEquations, branch->name(),
                                mul(k, dotSym(acrossSymbol)));
            } else {
                result.status = StateSpaceResult::Status::Unsupported;
                result.message =
                    QStringLiteral("Dependent A-type storage on a link is not supported yet.");
                return result;
            }
            break;
        case BranchType::T:
            if (!inTree) {
                elementalEquations.push_back(sub(across, mul(k, dotSym(flowSymbol))));
                addEquationText(result.elementalEquations, branchAcrossVariableText(*branch),
                                mul(k, dotSym(flowSymbol)));
            } else {
                result.status = StateSpaceResult::Status::Unsupported;
                result.message =
                    QStringLiteral("Dependent T-type storage on a tree branch is not supported yet.");
                return result;
            }
            break;
        case BranchType::D:
            elementalEquations.push_back(sub(flow, mul(k, across)));
            addEquationText(result.elementalEquations, branch->name(), mul(k, across));
            break;
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
        const std::unordered_set<NodeItem*> side = reachableTreeNodes(root, tree.treeBranches, twig);
        RCP<const Basic> cutSum = integer(0);
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
                cutSum = add(cutSum, signedThrough(branch, from, to));
            } else {
                cutSum = add(cutSum, signedThrough(branch, to, from));
            }
        }
        const RCP<const Basic> twigFlow = symOf(branchFlowSymbol(*twig));
        const QString twigFlowSymbol = branchFlowSymbol(*twig);
        const std::optional<RCP<const Basic>> solved = solveLinearFor(add(twigFlow, cutSum), twigFlow);
        if (!solved) {
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message =
                QStringLiteral("Could not solve continuity equation for %1.").arg(twig->name());
            return result;
        }
        recordReplacement(twigFlowSymbol, *solved, result.continuityEquations);
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
            treePathBetween(u, v, tree.treeBranches);
        if (!path) {
            result.status = StateSpaceResult::Status::GraphError;
            result.message = QStringLiteral("No tree path found for compatibility on link %1.")
                                 .arg(link->name());
            return result;
        }
        RCP<const Basic> loopSum = integer(0);
        NodeItem* cursor = u;
        for (BranchItem* edge : *path) {
            NodeItem* next = edge->from() == cursor ? edge->to() : edge->from();
            loopSum = add(loopSum, signedAcross(edge, cursor, next));
            cursor = next;
        }
        loopSum = add(loopSum, signedAcross(link, cursor, v));
        const QString linkAcrossSymbol = branchAcrossSymbol(*link);
        const RCP<const Basic> linkAcross = symOf(linkAcrossSymbol);
        const std::optional<RCP<const Basic>> solved = solveLinearFor(loopSum, linkAcross);
        if (!solved) {
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message = QStringLiteral("Could not solve compatibility equation for %1.")
                                 .arg(linkAcrossSymbol);
            return result;
        }
        recordReplacement(linkAcrossSymbol, *solved, result.compatibilityEquations);
        if (const QString displayAcross = branchAcrossVariableText(*link);
            displayAcross != linkAcrossSymbol) {
            replacements[displayAcross] = *solved;
        }
    }

    const SymEngine::map_basic_basic subMap = substitutionMap(replacements);
    std::vector<RCP<const Basic>> reducedElementals;
    reducedElementals.reserve(elementalEquations.size());
    for (const RCP<const Basic>& equation : elementalEquations) {
        reducedElementals.push_back(expand(subs(equation, subMap)));
    }

    std::unordered_map<QString, RCP<const Basic>> stateDots;
    std::vector<RCP<const Basic>> algebraics;
    for (const RCP<const Basic>& equation : reducedElementals) {
        bool matched = false;
        for (const ComputedState& state : computedStates) {
            const RCP<const Basic> dot = dotSym(state.symbol);
            const std::optional<RCP<const Basic>> solved = solveLinearFor(equation, dot);
            if (!solved) {
                continue;
            }
            stateDots[state.symbol] = *solved;
            matched = true;
            break;
        }
        if (!matched) {
            algebraics.push_back(equation);
        }
    }

    if (stateDots.size() != computedStates.size()) {
        result.status = StateSpaceResult::Status::SymbolicError;
        result.message = QStringLiteral("Could not derive all state equations.");
        return result;
    }

    const auto isStateSymbol = [&](const QString& symbol) {
        for (const ComputedState& state : computedStates) {
            if (state.symbol == symbol) {
                return true;
            }
        }
        return false;
    };
    const auto isInputSymbol = [&](const QString& symbol) {
        return std::find(result.inputs.begin(), result.inputs.end(), symbol) != result.inputs.end();
    };

    for (size_t pass = 0; pass < algebraics.size() + branches.size() && !algebraics.empty();
         ++pass) {
        bool progress = false;
        for (auto it = algebraics.begin(); it != algebraics.end();) {
            bool solvedEq = false;
            for (BranchItem* branch : branches) {
                if (!branch || branch->isActive()) {
                    continue;
                }
                const bool inTree = treeSet.count(branch) != 0;
                const QStringList candidates = {
                    branchFlowSymbol(*branch),
                    branchAcrossSymbol(*branch),
                    branchAcrossVariableText(*branch),
                };
                for (const QString& candidate : candidates) {
                    if (candidate.isEmpty() || candidate == QStringLiteral("0") ||
                        isStateSymbol(candidate) || isInputSymbol(candidate)) {
                        continue;
                    }
                    if (branch->branchType() == BranchType::A && inTree) {
                        continue;
                    }
                    if (branch->branchType() == BranchType::T && !inTree) {
                        continue;
                    }
                    const RCP<const Basic> primarySym = symOf(candidate);
                    const std::optional<RCP<const Basic>> solved =
                        solveLinearFor(*it, primarySym);
                    if (!solved) {
                        continue;
                    }
                    for (auto& [stateName, dotExpr] : stateDots) {
                        (void)stateName;
                        dotExpr = expand(subs(dotExpr, {{primarySym, *solved}}));
                    }
                    it = algebraics.erase(it);
                    solvedEq = true;
                    progress = true;
                    break;
                }
                if (solvedEq) {
                    break;
                }
            }
            if (!solvedEq) {
                ++it;
            }
        }
        if (!progress) {
            break;
        }
    }

    if (!algebraics.empty()) {
        result.status = StateSpaceResult::Status::SymbolicError;
        result.message = QStringLiteral("Could not eliminate all algebraic variables.");
        return result;
    }

    for (const ComputedState& state : computedStates) {
        addEquationText(result.stateEquations, dotName(state.symbol), stateDots.at(state.symbol));
    }

    if (!computedStates.empty()) {
        QStringList xDotEntries;
        QStringList xEntries;
        std::vector<QStringList> aRows;
        std::vector<QStringList> bRows;
        QStringList uEntries;
        xDotEntries.reserve(static_cast<int>(computedStates.size()));
        xEntries.reserve(static_cast<int>(computedStates.size()));
        aRows.reserve(computedStates.size());
        for (int i = 0; i < result.inputs.size(); ++i) {
            const QString& input = result.inputs.at(i);
            uEntries.push_back(latexMathSymbol(
                i < result.inputLabels.size() ? result.inputLabels.at(i) : input));
        }
        if (!result.inputs.isEmpty()) {
            bRows.reserve(computedStates.size());
        }
        for (const ComputedState& state : computedStates) {
            const RCP<const Basic> rhs = stateDots.at(state.symbol);
            xDotEntries.push_back(latexMathSymbol(dotName(state.symbol)));
            xEntries.push_back(latexMathSymbol(state.symbol));
            QStringList aRow;
            aRow.reserve(static_cast<int>(computedStates.size()));
            for (const ComputedState& col : computedStates) {
                aRow.push_back(latexCoeffExpr(linearCoeff(rhs, symOf(col.symbol))));
            }
            aRows.push_back(aRow);
            if (!result.inputs.isEmpty()) {
                QStringList bRow;
                bRow.reserve(result.inputs.size());
                for (const QString& input : result.inputs) {
                    bRow.push_back(latexCoeffExpr(linearCoeff(rhs, symOf(input))));
                }
                bRows.push_back(bRow);
            }
        }
        QString latex = QStringLiteral("$");
        latex += latexColumnVector(xDotEntries);
        latex += QStringLiteral(" = ");
        latex += latexBmatrix(aRows);
        latex += QStringLiteral(" ");
        latex += latexColumnVector(xEntries);
        if (!result.inputs.isEmpty()) {
            latex += QStringLiteral(" + ");
            latex += latexBmatrix(bRows);
            latex += QStringLiteral(" ");
            latex += latexColumnVector(uEntries);
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

namespace {

const bool kStateSpaceSelfCheck = [] {
    RCP<const Basic> x = symbol("x");
    RCP<const Basic> y = symbol("y");
    RCP<const Basic> f1 = symbol("f1");
    RCP<const Basic> f2 = symbol("f2");
    const RCP<const Basic> eq = sub(add(x, mul(integer(2), y)), integer(3));
    const std::optional<RCP<const Basic>> solved = solveLinearFor(eq, y);
    assert(solved);
    assert(eq(*subs(eq, {{y, *solved}}), *integer(0)));
    const RCP<const Basic> continuity = add(f2, f1);
    const std::optional<RCP<const Basic>> f2Solved = solveLinearFor(continuity, f2);
    assert(f2Solved);
    assert(eq(*f2Solved, *neg(f1)));
    assert(latexMathSymbol(QStringLiteral("x_dot")) == QStringLiteral("\\dot{x}"));
    assert(latexCoeffExpr(QStringLiteral("m**(-1)")) == QStringLiteral("\\frac{1}{m}"));
    assert(latexCoeffExpr(QStringLiteral("1/m")) == QStringLiteral("\\frac{1}{m}"));
    assert(latexColumnVector({QStringLiteral("a"), QStringLiteral("b")}) ==
           QStringLiteral("\\begin{bmatrix}a \\\\ b\\end{bmatrix}"));
    return true;
}();

}  // namespace

}  // namespace lg
