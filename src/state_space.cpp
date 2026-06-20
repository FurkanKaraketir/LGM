#include "state_space.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <symengine/basic.h>
#include <symengine/integer.h>
#include <symengine/add.h>
#include <symengine/mul.h>
#include <symengine/parser.h>
#include <symengine/printers.h>
#include <symengine/printers/strprinter.h>
#include <symengine/subs.h>
#include <symengine/symbol.h>

#include <QDebug>

#include <functional>
#include <cassert>
#include <optional>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
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

void ssLog(const QString& stage, const QString& detail = {}) {
    if (detail.isEmpty()) {
        qDebug().noquote() << "[state_space]" << stage;
        return;
    }
    qDebug().noquote() << "[state_space]" << stage << "-" << detail;
}

void ssLogExprs(const QString& label, const std::vector<RCP<const Basic>>& exprs) {
    for (size_t i = 0; i < exprs.size(); ++i) {
        ssLog(label, QStringLiteral("[%1] 0 = %2").arg(i).arg(exprText(exprs[i])));
    }
}

QString dotName(const QString& base) {
    return base + QStringLiteral("_dot");
}

RCP<const Basic> dotSym(const QString& base) { return symOf(dotName(base)); }

QString storageStateSymbol(const BranchItem& branch, bool inTree) {
    const BranchType type = effectivePassiveBranchType(branch);
    if (type == BranchType::A && inTree) {
        return branchAcrossSymbol(branch);
    }
    return branchFlowSymbol(branch);
}

bool isStateBranch(const NormalTreeResult& tree, BranchItem* branch) {
    if (!branch || branch->isActive() || isTwoPortInternalBranch(*branch)) {
        return false;
    }
    const bool inTree =
        std::find(tree.treeBranches.begin(), tree.treeBranches.end(), branch) !=
        tree.treeBranches.end();
    const BranchType type = effectivePassiveBranchType(*branch);
    if (type == BranchType::A && inTree) {
        return true;
    }
    if (type == BranchType::T && !inTree) {
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
    const std::optional<RCP<const Basic>> across = branchAcrossExpression(*branch);
    if (!across) {
        return integer(0);
    }
    if (branch->from() == from && branch->to() == to) {
        return *across;
    }
    if (branch->from() == to && branch->to() == from) {
        return neg(*across);
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
        const std::optional<RCP<const Basic>> sym = trySymOf(name);
        if (!sym) {
            continue;
        }
        out[*sym] = expr;
    }
    return out;
}

std::unordered_map<QString, RCP<const Basic>> resolveReplacements(
    std::unordered_map<QString, RCP<const Basic>> replacements) {
    bool changed = true;
    while (changed) {
        changed = false;
        const SymEngine::map_basic_basic map = substitutionMap(replacements);
        for (auto& [name, expr] : replacements) {
            const RCP<const Basic> next = expand(subs(expr, map));
            if (!eq(*next, *expr)) {
                expr = next;
                changed = true;
            }
        }
    }
    return replacements;
}

RCP<const Basic> linearTimeDerivative(const RCP<const Basic>& expr,
                                      const std::vector<QString>& timedSymbols) {
    RCP<const Basic> result = integer(0);
    const RCP<const Basic> expanded = expand(expr);
    for (const QString& name : timedSymbols) {
        const RCP<const Basic> var = symOf(name);
        const RCP<const Basic> withVar = subs(expanded, {{var, integer(1)}});
        const RCP<const Basic> withoutVar = subs(expanded, {{var, integer(0)}});
        const RCP<const Basic> coeff = expand(sub(withVar, withoutVar));
        if (eq(*coeff, *integer(0))) {
            continue;
        }
        result = add(result, mul(coeff, dotSym(name)));
    }
    return expand(result);
}

SymEngine::map_basic_basic extendedSubstitutionMap(
    const std::unordered_map<QString, RCP<const Basic>>& replacements,
    const std::vector<QString>& timedSymbols) {
    const std::unordered_map<QString, RCP<const Basic>> resolved =
        resolveReplacements(replacements);
    SymEngine::map_basic_basic out = substitutionMap(resolved);
    for (const auto& [name, expr] : resolved) {
        out[symOf(dotName(name))] = linearTimeDerivative(expr, timedSymbols);
    }
    return out;
}

SymEngine::map_basic_basic dependentDotSubstitutionMap(
    const std::unordered_map<QString, RCP<const Basic>>& replacements,
    const std::vector<QString>& timedSymbols,
    const std::function<bool(const QString&)>& isPrimaryState) {
    SymEngine::map_basic_basic out;
    const std::unordered_map<QString, RCP<const Basic>> resolved =
        resolveReplacements(replacements);
    for (const auto& [name, expr] : resolved) {
        if (isPrimaryState(name)) {
            continue;
        }
        out[symOf(dotName(name))] = linearTimeDerivative(expr, timedSymbols);
    }
    return out;
}

bool matrixEntryIsZero(const QString& entry) {
    return entry.isEmpty() || entry == QStringLiteral("0");
}

bool matrixIsZero(const std::vector<QStringList>& rows) {
    for (const QStringList& row : rows) {
        for (const QString& entry : row) {
            if (!matrixEntryIsZero(entry)) {
                return false;
            }
        }
    }
    return true;
}

RCP<const Basic> linearCoeffRCP(const RCP<const Basic>& expr, const RCP<const Basic>& variable) {
    const RCP<const Basic> expanded = expand(expr);
    const RCP<const Basic> withVar = subs(expanded, {{variable, integer(1)}});
    const RCP<const Basic> withoutVar = subs(expanded, {{variable, integer(0)}});
    return expand(sub(withVar, withoutVar));
}

RCP<const Basic> linearRemainder(const RCP<const Basic>& expr,
                                 const std::vector<RCP<const Basic>>& linearVars) {
    RCP<const Basic> result = expand(expr);
    for (const RCP<const Basic>& var : linearVars) {
        const RCP<const Basic> coeff = linearCoeffRCP(result, var);
        if (!eq(*coeff, *integer(0))) {
            result = expand(sub(result, mul(coeff, var)));
        }
    }
    return expand(result);
}

std::optional<std::vector<RCP<const Basic>>> solveLinearSystem(
    std::vector<std::vector<RCP<const Basic>>> matrix, std::vector<RCP<const Basic>> rhs) {
    const size_t n = matrix.size();
    if (n == 0 || rhs.size() != n) {
        return std::nullopt;
    }
    for (const std::vector<RCP<const Basic>>& row : matrix) {
        if (row.size() != n) {
            return std::nullopt;
        }
    }
    // ponytail: symbolic Gaussian elimination, adequate for small state order
    for (size_t col = 0; col < n; ++col) {
        size_t pivot = col;
        for (size_t row = col + 1; row < n; ++row) {
            if (eq(*matrix[pivot][col], *integer(0)) && !eq(*matrix[row][col], *integer(0))) {
                pivot = row;
            }
        }
        if (eq(*matrix[pivot][col], *integer(0))) {
            return std::nullopt;
        }
        if (pivot != col) {
            std::swap(matrix[pivot], matrix[col]);
            std::swap(rhs[pivot], rhs[col]);
        }
        for (size_t row = col + 1; row < n; ++row) {
            if (eq(*matrix[row][col], *integer(0))) {
                continue;
            }
            const RCP<const Basic> factor = div(matrix[row][col], matrix[col][col]);
            for (size_t k = col; k < n; ++k) {
                matrix[row][k] = expand(sub(matrix[row][k], mul(factor, matrix[col][k])));
            }
            rhs[row] = expand(sub(rhs[row], mul(factor, rhs[col])));
        }
    }
    std::vector<RCP<const Basic>> solution(n);
    for (int row = static_cast<int>(n) - 1; row >= 0; --row) {
        RCP<const Basic> sum = rhs[static_cast<size_t>(row)];
        for (size_t col = static_cast<size_t>(row) + 1; col < n; ++col) {
            sum = expand(sub(sum, mul(matrix[static_cast<size_t>(row)][col], solution[col])));
        }
        if (eq(*matrix[static_cast<size_t>(row)][static_cast<size_t>(row)], *integer(0))) {
            return std::nullopt;
        }
        solution[static_cast<size_t>(row)] =
            div(sum, matrix[static_cast<size_t>(row)][static_cast<size_t>(row)]);
    }
    return solution;
}

void resolveStateDotCoupling(const std::vector<QString>& stateSymbols,
                             std::unordered_map<QString, RCP<const Basic>>& stateDots) {
    const size_t n = stateSymbols.size();
    if (n == 0) {
        return;
    }
    std::vector<RCP<const Basic>> dotVars;
    dotVars.reserve(n);
    for (const QString& symbol : stateSymbols) {
        dotVars.push_back(dotSym(symbol));
    }

    auto hasCrossDot = [&](size_t i) {
        const RCP<const Basic> expr = stateDots.at(stateSymbols[i]);
        for (size_t j = 0; j < n; ++j) {
            if (i != j && !eq(*linearCoeffRCP(expr, dotVars[j]), *integer(0))) {
                return true;
            }
        }
        return false;
    };

    // ponytail: decoupled self-coupling (e.g. C4/C5 on V2) — solve per state first
    for (size_t i = 0; i < n; ++i) {
        if (hasCrossDot(i)) {
            continue;
        }
        const RCP<const Basic> expr = stateDots.at(stateSymbols[i]);
        const RCP<const Basic> t_ii = linearCoeffRCP(expr, dotVars[i]);
        if (eq(*t_ii, *integer(0))) {
            continue;
        }
        const RCP<const Basic> rhs = linearRemainder(expr, dotVars);
        stateDots[stateSymbols[i]] = expand(div(rhs, sub(integer(1), t_ii)));
    }

    std::vector<size_t> coupled;
    coupled.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const RCP<const Basic> expr = stateDots.at(stateSymbols[i]);
        if (hasCrossDot(i) || !eq(*linearCoeffRCP(expr, dotVars[i]), *integer(0))) {
            coupled.push_back(i);
        }
    }
    if (coupled.empty()) {
        return;
    }

    const size_t m = coupled.size();
    std::vector<std::vector<RCP<const Basic>>> matrix(
        m, std::vector<RCP<const Basic>>(m, integer(0)));
    std::vector<RCP<const Basic>> rhs(m, integer(0));
    for (size_t ri = 0; ri < m; ++ri) {
        const size_t i = coupled[ri];
        const RCP<const Basic> expr = stateDots.at(stateSymbols[i]);
        rhs[ri] = linearRemainder(expr, dotVars);
        for (size_t cj = 0; cj < m; ++cj) {
            const size_t j = coupled[cj];
            const RCP<const Basic> t_ij = linearCoeffRCP(expr, dotVars[j]);
            matrix[ri][cj] = (ri == cj) ? sub(integer(1), t_ij) : neg(t_ij);
        }
    }
    const std::optional<std::vector<RCP<const Basic>>> solved = solveLinearSystem(matrix, rhs);
    if (!solved) {
        for (size_t ri = 0; ri < m; ++ri) {
            QString row;
            for (size_t cj = 0; cj < m; ++cj) {
                if (cj != 0) {
                    row += QStringLiteral(" ");
                }
                row += exprText(matrix[ri][cj]);
            }
            ssLog(QStringLiteral("coupling_fail_row"),
                  QStringLiteral("%1: [%2] rhs=%3")
                      .arg(stateSymbols[coupled[ri]], row, exprText(rhs[ri])));
        }
        throw std::runtime_error("could not resolve state derivative coupling");
    }
    for (size_t ri = 0; ri < m; ++ri) {
        stateDots[stateSymbols[coupled[ri]]] = expand((*solved)[ri]);
    }
}

bool substitutionIsCircular(const RCP<const Basic>& target, const RCP<const Basic>& primarySym,
                            const RCP<const Basic>& solved) {
    return eq(*expand(subs(target, {{primarySym, solved}})), *target);
}

using SubstitutionFilter = std::function<bool(const QString& targetName,
                                              const RCP<const Basic>& targetExpr,
                                              const RCP<const Basic>& primarySym,
                                              const RCP<const Basic>& solved)>;

void eliminateSymbolsInto(
    std::unordered_map<QString, RCP<const Basic>>& targets,
    const std::vector<RCP<const Basic>>& relations,
    const std::vector<QString>& candidates,
    const std::function<bool(const QString&)>& canEliminate,
    const std::function<bool(const RCP<const Basic>&)>& acceptSolution = {},
    const SubstitutionFilter& acceptSubstitution = {}) {
    if (candidates.empty() || relations.empty()) {
        return;
    }
    const size_t maxPasses = relations.size() * candidates.size() + candidates.size();
    for (size_t pass = 0; pass < maxPasses; ++pass) {
        bool progress = false;
        for (const RCP<const Basic>& relation : relations) {
            bool solvedRelation = false;
            for (const QString& candidate : candidates) {
                if (!canEliminate(candidate)) {
                    continue;
                }
                const RCP<const Basic> primarySym = symOf(candidate);
                const std::optional<RCP<const Basic>> solved =
                    solveLinearFor(relation, primarySym);
                if (!solved) {
                    continue;
                }
                if (acceptSolution && !acceptSolution(*solved)) {
                    continue;
                }
                bool changed = false;
                for (auto& [name, expr] : targets) {
                    if (acceptSubstitution &&
                        !acceptSubstitution(name, expr, primarySym, *solved)) {
                        continue;
                    }
                    if (substitutionIsCircular(expr, primarySym, *solved)) {
                        continue;
                    }
                    const RCP<const Basic> next = expand(subs(expr, {{primarySym, *solved}}));
                    if (!eq(*next, *expr)) {
                        expr = next;
                        changed = true;
                    }
                }
                if (!changed) {
                    continue;
                }
                progress = true;
                solvedRelation = true;
                break;
            }
            (void)solvedRelation;
        }
        if (!progress) {
            break;
        }
    }
}

QString branchTautologyAcrossSymbol(const BranchItem& branch) {
    const QString text = branchAcrossVariableText(branch).trimmed();
    if (text.isEmpty() || text == QStringLiteral("0") || isValidVariableSymbol(text)) {
        return {};
    }
    return branchFlowSymbol(branch) + QStringLiteral("_a");
}

void eliminateBranchSymbolsInto(
    std::unordered_map<QString, RCP<const Basic>>& targets,
    const std::vector<RCP<const Basic>>& relations,
    const std::vector<BranchItem*>& branches,
    const std::unordered_set<BranchItem*>& treeSet,
    const std::function<bool(const QString&)>& canEliminate) {
    std::vector<QString> candidates;
    candidates.reserve(branches.size() * 3);
    for (BranchItem* branch : branches) {
        if (!branch || branch->isActive() || isTwoPortInternalBranch(*branch)) {
            continue;
        }
        const bool inTree = treeSet.count(branch) != 0;
        const BranchType type = effectivePassiveBranchType(*branch);
        if (type == BranchType::A && inTree) {
            continue;
        }
        if (type == BranchType::T && !inTree) {
            continue;
        }
        for (const QString& candidate :
             {branchFlowSymbol(*branch), branchAcrossSymbol(*branch)}) {
            if (!candidate.isEmpty()) {
                candidates.push_back(candidate);
            }
        }
        const QString acrossText = branchAcrossVariableText(*branch);
        if (isValidVariableSymbol(acrossText)) {
            candidates.push_back(acrossText);
        }
    }
    eliminateSymbolsInto(targets, relations, candidates, canEliminate);
}

std::vector<QString> collectNodeAcrossSymbols(const std::vector<NodeItem*>& nodes) {
    std::vector<QString> symbols;
    for (NodeItem* node : nodes) {
        if (!node || node->isGround()) {
            continue;
        }
        const QString across = node->acrossVariable().trimmed();
        if (across.isEmpty() || across == QStringLiteral("0")) {
            continue;
        }
        if (isValidVariableSymbol(across)) {
            symbols.push_back(across);
        }
    }
    return symbols;
}

std::vector<RCP<const Basic>> constraintRelations(
    const std::unordered_map<QString, RCP<const Basic>>& replacements) {
    std::vector<RCP<const Basic>> relations;
    const std::unordered_map<QString, RCP<const Basic>> resolved =
        resolveReplacements(replacements);
    relations.reserve(resolved.size());
    for (const auto& [name, expr] : resolved) {
        if (!isValidVariableSymbol(name)) {
            continue;
        }
        relations.push_back(expand(sub(symOf(name), expr)));
    }
    return relations;
}

QString latexMathSymbol(const QString& name) {
    if (name.endsWith(QStringLiteral("_dot"))) {
        return QStringLiteral("\\dot{%1}").arg(latexMathSymbol(name.left(name.size() - 4)));
    }
    return name;
}

QString latexInputDotLabel(const QString& inputLabel) {
    if (inputLabel.endsWith(QStringLiteral("(t)"))) {
        return QStringLiteral("\\dot{%1}(t)").arg(inputLabel.left(inputLabel.size() - 3));
    }
    return latexMathSymbol(inputLabel + QStringLiteral("_dot"));
}

QString displayExprText(const RCP<const Basic>& expr, const QStringList& inputs,
                        const QStringList& inputLabels) {
    QString text = exprText(expr);
    for (int i = 0; i < inputs.size(); ++i) {
        const QString& input = inputs.at(i);
        const QString label =
            i < inputLabels.size() ? inputLabels.at(i) : input + QStringLiteral("(t)");
        text.replace(dotName(input), inputDerivativeDisplay(input, label));
    }
    return text;
}

QString latexCoeff(const RCP<const Basic>& coeff) {
    if (eq(*coeff, *integer(0))) {
        return QStringLiteral("0");
    }
    return QString::fromStdString(SymEngine::latex(*coeff));
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
        ssLog(QStringLiteral("exception"), QString::fromUtf8(e.what()));
        result.status = StateSpaceResult::Status::SymbolicError;
        result.message = QStringLiteral("Symbolic processing failed: %1")
                             .arg(QString::fromUtf8(e.what()));
        return result;
    } catch (...) {
        ssLog(QStringLiteral("exception"), QStringLiteral("unknown"));
        result.status = StateSpaceResult::Status::SymbolicError;
        result.message = QStringLiteral("Symbolic processing failed.");
        return result;
    }
}

StateSpaceResult computeStateSpaceImpl(const NormalTreeResult& tree,
                                       const std::vector<NodeItem*>& nodes,
                                       const std::vector<BranchItem*>& branches,
                                       const std::vector<TwoPortItem*>& twoPorts) {
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
        if (!isStateBranch(tree, branch)) {
            continue;
        }
        const bool inTree = treeSet.count(branch) != 0;
        computedStates.push_back({storageStateSymbol(*branch, inTree), branch});
    }

    {
        QStringList stateDesc;
        for (const ComputedState& state : computedStates) {
            const QString branchName =
                state.branch ? state.branch->name() : QStringLiteral("?");
            stateDesc.push_back(QStringLiteral("%1 (%2)").arg(state.symbol, branchName));
        }
        ssLog(QStringLiteral("begin"),
              QStringLiteral("states=[%1] tree_branches=%2")
                  .arg(stateDesc.join(QStringLiteral(", ")))
                  .arg(tree.treeBranches.size()));
    }

    std::unordered_map<QString, RCP<const Basic>> replacements;
    struct BranchElemental {
        BranchItem* branch = nullptr;
        RCP<const Basic> expr;
    };
    std::vector<BranchElemental> elementalEquations;

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
        if (!branch || branch->isActive() || isTwoPortInternalBranch(*branch)) {
            continue;
        }
        const RCP<const Basic> k = branchElementConstantExpr(*branch);
        const SystemType systemType = lg::branchSystemType(*branch);
        const BranchType type = effectivePassiveBranchType(*branch);
        const RCP<const Basic> coeff = elementalConstantCoeff(systemType, type, k);
        const QString flowSymbol = branchFlowSymbol(*branch);
        const RCP<const Basic> flow = symOf(flowSymbol);
        const QString acrossSymbol = branchAcrossSymbol(*branch);
        const RCP<const Basic> across = symOf(acrossSymbol);

        switch (type) {
        case BranchType::A:
            elementalEquations.push_back(
                {branch, sub(flow, mul(k, dotSym(acrossSymbol)))});
            addEquationText(result.elementalEquations, branch->name(), mul(k, dotSym(acrossSymbol)));
            break;
        case BranchType::T: {
            const RCP<const Basic> displayCoeff =
                elementalConstantInNumerator(systemType, BranchType::T) ? div(integer(1), k) : k;
            elementalEquations.push_back(
                {branch, sub(dotSym(flowSymbol), mul(coeff, across))});
            addEquationText(result.elementalEquations, branchAcrossVariableText(*branch),
                            mul(displayCoeff, dotSym(flowSymbol)));
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
        const RCP<const Basic> k = twoPortModulusExpr(*twoPort);
        const RCP<const Basic> leftAcross = symOf(branchAcrossSymbol(*left));
        const RCP<const Basic> rightAcross = symOf(branchAcrossSymbol(*right));
        const RCP<const Basic> leftFlow = symOf(branchFlowSymbol(*left));
        const RCP<const Basic> rightFlow = symOf(branchFlowSymbol(*right));
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
        ssLog(QStringLiteral("inputs"), result.inputs.join(QStringLiteral(", ")));
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
            ssLog(QStringLiteral("error"), result.message);
            return result;
        }
        recordReplacement(twigFlowSymbol, *solved, result.continuityEquations);
    }

    // Tree A-type voltage sources fix a node across to the input symbol; bind before
    // compatibility so compound link voltages (e.g. V1 - V3) differentiate w.r.t. V2(t).
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
        const RCP<const Basic> inputExpr = symOf(input);
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

    // Tree branches with compound across (e.g. i_R2_a = V4 - V3) before link compatibility.
    for (BranchItem* branch : tree.treeBranches) {
        if (!branch || branch->isActive()) {
            continue;
        }
        if (!usesSyntheticAcrossSymbol(*branch)) {
            continue;
        }
        const QString acrossSymbol = branchAcrossSymbol(*branch);
        const std::optional<RCP<const Basic>> defExpr = branchAcrossExpression(*branch);
        if (!defExpr) {
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message = QStringLiteral("Could not parse across variable for %1.")
                                 .arg(branch->name());
            return result;
        }
        const SymEngine::map_basic_basic bindMap =
            substitutionMap(resolveReplacements(replacements));
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
        // Close the loop along the link back to u (cursor is v after the tree walk).
        loopSum = add(loopSum, signedAcross(link, cursor, u));
        const QString linkAcrossSymbol = branchAcrossSymbol(*link);

        auto recordSyntheticAcross = [&]() -> bool {
            if (!usesSyntheticAcrossSymbol(*link)) {
                return false;
            }
            const std::optional<RCP<const Basic>> defExpr = branchAcrossExpression(*link);
            if (!defExpr) {
                result.status = StateSpaceResult::Status::SymbolicError;
                result.message = QStringLiteral("Could not parse across variable for %1.")
                                     .arg(link->name());
                return true;
            }
            const SymEngine::map_basic_basic bindMap =
                substitutionMap(resolveReplacements(replacements));
            recordReplacement(linkAcrossSymbol, expand(subs(*defExpr, bindMap)),
                              result.compatibilityEquations);
            return true;
        };

        // Co-tree current source: through is the input; define compound voltage only.
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
            substitutionMap(resolveReplacements(replacements));
        loopSum = expand(subs(loopSum, bindMap));

        const RCP<const Basic> linkAcross = symOf(linkAcrossSymbol);
        std::optional<RCP<const Basic>> solved = solveLinearFor(loopSum, linkAcross);
        if (!solved) {
            // ponytail: parallel link shares its twig's across symbol, so KVL is 0=0; skip.
            if (eq(*expand(loopSum), *integer(0))) {
                continue;
            }
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message = QStringLiteral("Could not solve compatibility equation for %1.")
                                 .arg(linkAcrossSymbol);
            ssLog(QStringLiteral("error"), result.message + QStringLiteral(" loopSum=0=") +
                                               exprText(loopSum));
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
        substitutionMap(resolveReplacements(replacements));
    const SymEngine::map_basic_basic subMap = extendedSubstitutionMap(replacements, timedSymbols);
    std::vector<RCP<const Basic>> reducedElementals;
    reducedElementals.reserve(elementalEquations.size());
    for (const BranchElemental& elemental : elementalEquations) {
        reducedElementals.push_back(expand(subs(elemental.expr, subMap)));
    }

    ssLog(QStringLiteral("replacements"), QString::number(replacements.size()));
    ssLogExprs(QStringLiteral("reduced_elemental"), reducedElementals);

    std::unordered_map<QString, RCP<const Basic>> stateDots;
    std::vector<RCP<const Basic>> algebraics;
    for (const BranchElemental& elemental : elementalEquations) {
        // ponytail: value-only subs — replacing state dots via continuity first destroys the
        // branch elemental (e.g. L7 i_L7_dot) before we can solve for the storage derivative.
        const RCP<const Basic> reduced = expand(subs(elemental.expr, valueSubMap));
        if (!elemental.branch) {
            algebraics.push_back(reduced);
            continue;
        }
        bool matched = false;
        for (const ComputedState& state : computedStates) {
            if (state.branch != elemental.branch) {
                continue;
            }
            const RCP<const Basic> dot = dotSym(state.symbol);
            const std::optional<RCP<const Basic>> solved = solveLinearFor(reduced, dot);
            if (!solved) {
                continue;
            }
            if (stateDots.count(state.symbol) != 0) {
                ssLog(QStringLiteral("warn"),
                      QStringLiteral("duplicate %1 from %2, keeping first")
                          .arg(dotName(state.symbol), elemental.branch->name()));
                matched = true;
                break;
            }
            stateDots[state.symbol] = *solved;
            ssLog(QStringLiteral("matched"),
                  QStringLiteral("%1 from branch %2: 0 = %3")
                      .arg(dotName(state.symbol), elemental.branch->name(), exprText(reduced)));
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
        ssLog(QStringLiteral("error"),
              QStringLiteral("missing state dots: %1").arg(missing.join(QStringLiteral(", "))));
        ssLogExprs(QStringLiteral("algebraic"), algebraics);
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
    const auto isInputLike = [&](const QString& symbol) {
        if (std::find(result.inputs.begin(), result.inputs.end(), symbol) != result.inputs.end()) {
            return true;
        }
        for (const QString& input : result.inputs) {
            if (symbol == dotName(input)) {
                return true;
            }
        }
        return false;
    };
    const auto isStateDotLike = [&](const QString& symbol) {
        if (!symbol.endsWith(QStringLiteral("_dot"))) {
            return false;
        }
        return isStateSymbol(symbol.left(symbol.size() - 4));
    };
    const auto canEliminateBranchSymbol = [&](const QString& candidate) {
        return !candidate.isEmpty() && candidate != QStringLiteral("0") &&
               isValidVariableSymbol(candidate) && !isStateSymbol(candidate) &&
               !isInputLike(candidate) && !isStateDotLike(candidate);
    };

    const std::vector<QString> nodeAcrossSymbols = collectNodeAcrossSymbols(nodes);
    const std::vector<RCP<const Basic>> constraintEqs = constraintRelations(replacements);
    auto allRelations = [&](const std::vector<RCP<const Basic>>& base) {
        std::vector<RCP<const Basic>> merged = base;
        merged.insert(merged.end(), constraintEqs.begin(), constraintEqs.end());
        return merged;
    };
    const auto dependentDotMap = dependentDotSubstitutionMap(
        replacements, timedSymbols, isStateSymbol);

    std::unordered_map<QString, BranchItem*> stateBranchBySymbol;
    for (const ComputedState& state : computedStates) {
        stateBranchBySymbol[state.symbol] = state.branch;
    }
    // ponytail: block only synthetic branch across (i_L7_a), not the state flow (i_L7).
    const auto rejectTautologicalNodeElim = [&](const QString& targetName,
                                                const RCP<const Basic>& targetExpr,
                                                const RCP<const Basic>& primarySym,
                                                const RCP<const Basic>& solved) {
        const auto it = stateBranchBySymbol.find(targetName);
        if (it != stateBranchBySymbol.end() && it->second) {
            const QString across = branchTautologyAcrossSymbol(*it->second);
            if (!across.isEmpty() &&
                !eq(*linearCoeffRCP(solved, symOf(across)), *integer(0))) {
                return false;
            }
        }
        return !substitutionIsCircular(targetExpr, primarySym, solved);
    };

    eliminateBranchSymbolsInto(stateDots, allRelations(algebraics), branches, treeSet,
                               canEliminateBranchSymbol);
    eliminateSymbolsInto(stateDots, allRelations(algebraics), nodeAcrossSymbols,
                         canEliminateBranchSymbol, {}, rejectTautologicalNodeElim);

    for (auto& [name, expr] : stateDots) {
        (void)name;
        expr = expand(subs(expr, valueSubMap));
        expr = expand(subs(expr, dependentDotMap));
    }
    eliminateBranchSymbolsInto(stateDots, allRelations(reducedElementals), branches, treeSet,
                               canEliminateBranchSymbol);
    eliminateSymbolsInto(stateDots, allRelations(reducedElementals), nodeAcrossSymbols,
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

    ssLog(QStringLiteral("pre_coupling"), QStringLiteral("algebraics=%1").arg(algebraics.size()));
    for (const ComputedState& state : computedStates) {
        ssLog(QStringLiteral("state_dot"),
              QStringLiteral("%1 = %2")
                  .arg(dotName(state.symbol), exprText(stateDots.at(state.symbol))));
    }

    resolveStateDotCoupling(stateSymbols, stateDots);

    for (auto& [name, expr] : stateDots) {
        (void)name;
        expr = expand(subs(expr, dependentDotMap));
    }

    const std::vector<RCP<const Basic>> allReductionRelations = allRelations(reducedElementals);
    eliminateSymbolsInto(stateDots, allReductionRelations, nodeAcrossSymbols,
                         canEliminateBranchSymbol, {}, rejectTautologicalNodeElim);
    eliminateBranchSymbolsInto(stateDots, allReductionRelations, branches, treeSet,
                               canEliminateBranchSymbol);
    for (size_t pass = 0; pass < 4; ++pass) {
        bool changed = false;
        for (auto& [name, expr] : stateDots) {
            (void)name;
            const RCP<const Basic> next =
                expand(subs(expand(subs(expr, valueSubMap)), dependentDotMap));
            if (!eq(*next, *expr)) {
                expr = next;
                changed = true;
            }
        }
        eliminateBranchSymbolsInto(stateDots, allReductionRelations, branches, treeSet,
                                   canEliminateBranchSymbol);
        eliminateSymbolsInto(stateDots, allReductionRelations, nodeAcrossSymbols,
                             canEliminateBranchSymbol, {}, rejectTautologicalNodeElim);
        if (!changed) {
            break;
        }
    }
    resolveStateDotCoupling(stateSymbols, stateDots);
    for (auto& [name, expr] : stateDots) {
        (void)name;
        expr = expand(subs(expr, dependentDotMap));
    }

    // Every state equation must reduce to a linear combination of states, inputs and
    // input derivatives. Anything left over is an un-eliminated branch variable that the
    // matrix extraction below would silently drop, so flag it instead of emitting a wrong A/B.
    std::vector<RCP<const Basic>> stateInputVars;
    stateInputVars.reserve(stateSymbols.size() + 2 * static_cast<size_t>(result.inputs.size()));
    for (const QString& symbol : stateSymbols) {
        stateInputVars.push_back(symOf(symbol));
    }
    for (const QString& input : result.inputs) {
        stateInputVars.push_back(symOf(input));
        stateInputVars.push_back(symOf(dotName(input)));
    }
    for (const ComputedState& state : computedStates) {
        const RCP<const Basic> remainder =
            linearRemainder(stateDots.at(state.symbol), stateInputVars);
        if (!eq(*remainder, *integer(0))) {
            result.status = StateSpaceResult::Status::SymbolicError;
            result.message = QStringLiteral("State equation for %1 has unresolved terms: %2.")
                                 .arg(dotName(state.symbol), exprText(remainder));
            ssLog(QStringLiteral("error"), result.message);
            ssLog(QStringLiteral("state_dot_full"),
                  QStringLiteral("%1 = %2")
                      .arg(dotName(state.symbol), exprText(stateDots.at(state.symbol))));
            return result;
        }
    }

    ssLog(QStringLiteral("ok"), QStringLiteral("order=%1").arg(computedStates.size()));

    for (const ComputedState& state : computedStates) {
        const QString lhs = dotName(state.symbol);
        const QString rhs = displayExprText(stateDots.at(state.symbol), result.inputs,
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
            uEntries.push_back(latexMathSymbol(
                i < result.inputLabels.size() ? result.inputLabels.at(i) : input));
            uDotEntries.push_back(i < result.inputLabels.size()
                                      ? latexInputDotLabel(result.inputLabels.at(i))
                                      : latexMathSymbol(dotName(input)));
        }
        if (!result.inputs.isEmpty()) {
            bRows.reserve(computedStates.size());
            eRows.reserve(computedStates.size());
        }
        for (const ComputedState& state : computedStates) {
            const RCP<const Basic> rhs = stateDots.at(state.symbol);
            xDotEntries.push_back(latexMathSymbol(dotName(state.symbol)));
            xEntries.push_back(latexMathSymbol(state.symbol));
            QStringList aRow;
            aRow.reserve(static_cast<int>(computedStates.size()));
            for (const ComputedState& col : computedStates) {
                aRow.push_back(latexCoeff(linearCoeffRCP(rhs, symOf(col.symbol))));
            }
            aRows.push_back(aRow);
            if (!result.inputs.isEmpty()) {
                QStringList bRow;
                QStringList eRow;
                bRow.reserve(result.inputs.size());
                eRow.reserve(result.inputs.size());
                for (const QString& input : result.inputs) {
                    bRow.push_back(latexCoeff(linearCoeffRCP(rhs, symOf(input))));
                    eRow.push_back(latexCoeff(linearCoeffRCP(rhs, symOf(dotName(input)))));
                }
                bRows.push_back(bRow);
                eRows.push_back(eRow);
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
            if (!matrixIsZero(eRows)) {
                latex += QStringLiteral(" + ");
                latex += latexBmatrix(eRows);
                latex += QStringLiteral(" ");
                latex += latexColumnVector(uDotEntries);
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
    {
        // Tree A->B (e_tree) plus link A->B (e_link): e_tree - e_link = 0.
        RCP<const Basic> eTree = symbol("e_tree");
        RCP<const Basic> eLink = symbol("e_link");
        const RCP<const Basic> loop = sub(eTree, eLink);
        const std::optional<RCP<const Basic>> eLinkSolved = solveLinearFor(loop, eLink);
        assert(eLinkSolved);
        assert(eq(*eLinkSolved, *eTree));
    }
    {
        RCP<const Basic> v1 = symbol("v1");
        const RCP<const Basic> degenerateLoop = sub(v1, v1);
        assert(eq(*expand(degenerateLoop), *integer(0)));
        assert(!solveLinearFor(degenerateLoop, v1));
    }
    assert(latexMathSymbol(QStringLiteral("x_dot")) == QStringLiteral("\\dot{x}"));
    assert(latexCoeff(div(integer(1), symbol("m"))) == QStringLiteral("\\frac{1}{m}"));
    // Compound coefficient with a nested denominator: the old regex printer mangled
    // this into "\frac{-1}{(L7} \cdot \frac{(1 + L3}{L7))}".
    {
        const RCP<const Basic> nested =
            div(integer(-1), mul(symbol("L7"), add(integer(1), div(symbol("L3"), symbol("L7")))));
        const QString tex = latexCoeff(nested);
        assert(tex.startsWith(QStringLiteral("\\frac{")));
        assert(!tex.contains(QStringLiteral("\\cdot")));
        assert(tex.count(QLatin1Char('(')) == tex.count(QLatin1Char(')')));
    }
    assert(latexColumnVector({QStringLiteral("a"), QStringLiteral("b")}) ==
           QStringLiteral("\\begin{bmatrix}a \\\\ b\\end{bmatrix}"));
    const RCP<const Basic> leadLagAcross = sub(symbol("V1"), symbol("v2"));
    const RCP<const Basic> leadLagAcrossDot =
        linearTimeDerivative(leadLagAcross, {QStringLiteral("V1"), QStringLiteral("v2")});
    assert(eq(*leadLagAcrossDot, *sub(dotSym(QStringLiteral("V1")), dotSym(QStringLiteral("v2")))));
    {
        std::unordered_map<QString, RCP<const Basic>> reps;
        reps[QStringLiteral("C1_a")] = leadLagAcross;
        const SymEngine::map_basic_basic map =
            extendedSubstitutionMap(reps, {QStringLiteral("V1"), QStringLiteral("v2")});
        const RCP<const Basic> elem =
            sub(symbol("f1"), mul(symbol("C1"), map.at(symOf(dotName(QStringLiteral("C1_a"))))));
        const RCP<const Basic> expected =
            sub(symbol("f1"), mul(symbol("C1"), leadLagAcrossDot));
        assert(eq(*expand(elem), *expand(expected)));
    }
    {
        // Node bound to active source input: i1_a = V1 - V3 must resolve to Vs2 - V3 for Vs2_dot.
        std::unordered_map<QString, RCP<const Basic>> reps;
        reps[QStringLiteral("V1")] = symbol("Vs2");
        reps[QStringLiteral("i1_a")] = sub(symbol("V1"), symbol("V3"));
        const auto resolved = resolveReplacements(reps);
        assert(eq(*expand(resolved.at(QStringLiteral("i1_a"))),
                   *sub(symbol("Vs2"), symbol("V3"))));
        const SymEngine::map_basic_basic map = extendedSubstitutionMap(
            {{QStringLiteral("i1_a"), resolved.at(QStringLiteral("i1_a"))}},
            {QStringLiteral("Vs2"), QStringLiteral("V3")});
        assert(eq(*map.at(symOf(QStringLiteral("i1_a_dot"))),
                   *sub(dotSym(QStringLiteral("Vs2")), dotSym(QStringLiteral("V3")))));
        const RCP<const Basic> v3dot =
            div(mul(symbol("C1"), dotSym(QStringLiteral("Vs2"))), add(symbol("C1"), symbol("C2")));
        const RCP<const Basic> elem = sub(mul(symbol("C2"), dotSym(QStringLiteral("V3"))),
                                          mul(symbol("C1"), map.at(symOf(QStringLiteral("i1_a_dot")))));
        const std::optional<RCP<const Basic>> v3dotSolved =
            solveLinearFor(elem, dotSym(QStringLiteral("V3")));
        assert(v3dotSolved);
        assert(eq(*expand(*v3dotSolved), *expand(v3dot)));
    }
    {
        // Algebraic node voltages (e.g. V3) eliminate from state equations.
        std::unordered_map<QString, RCP<const Basic>> dots;
        dots[QStringLiteral("i_L7")] = add(div(symbol("V1"), symbol("L7")),
                                           div(neg(symbol("V3")), symbol("L7")));
        const std::vector<RCP<const Basic>> relations = {
            sub(symbol("V3"), symbol("V2"))};
        eliminateSymbolsInto(dots, relations, {QStringLiteral("V3")},
                             [](const QString&) { return true; });
        assert(eq(*expand(dots.at(QStringLiteral("i_L7"))),
                   *div(sub(symbol("V1"), symbol("V2")), symbol("L7"))));
    }
    {
        // Final-equation guard: a fully reduced rhs (states/inputs with constant coeffs)
        // has zero remainder, while a stray branch variable survives it.
        const std::vector<RCP<const Basic>> stateInputVars = {symbol("V2"), symbol("I8")};
        const RCP<const Basic> reduced =
            add(mul(symbol("R1"), symbol("V2")), mul(symbol("C5"), symbol("I8")));
        assert(eq(*linearRemainder(reduced, stateInputVars), *integer(0)));
        const RCP<const Basic> leftover = add(reduced, symbol("i7"));
        assert(eq(*linearRemainder(leftover, stateInputVars), *symbol("i7")));
    }
    {
        // Dependent storage: v2_dot appears on both sides (C4/C5 self-coupling).
        std::unordered_map<QString, RCP<const Basic>> dots;
        dots[QStringLiteral("v2")] =
            sub(dotSym(QStringLiteral("V1")),
                div(sub(mul(symbol("C4"), dotSym(QStringLiteral("v2"))),
                        mul(symbol("R4"), sub(symbol("V1"), symbol("v2")))),
                    symbol("C5")));
        resolveStateDotCoupling({QStringLiteral("v2")}, dots);
        assert(eq(*linearCoeffRCP(dots.at(QStringLiteral("v2")), dotSym(QStringLiteral("v2"))),
               *integer(0)));
        assert(eq(*linearCoeffRCP(dots.at(QStringLiteral("v2")), dotSym(QStringLiteral("V1"))),
               *div(symbol("C5"), add(symbol("C4"), symbol("C5")))));
    }
    {
        // Tree capacitor uses synthetic i_C5_a; link C4 uses i_C4_a = -V2 — both must bind for KCL.
        std::unordered_map<QString, RCP<const Basic>> reps;
        reps[QStringLiteral("V1")] = symbol("Vs");
        reps[QStringLiteral("i_C5_a")] = sub(symbol("V1"), symbol("V2"));
        reps[QStringLiteral("i_C4_a")] = neg(symbol("V2"));
        const std::unordered_map<QString, RCP<const Basic>> resolved = resolveReplacements(reps);
        assert(eq(*resolved.at(QStringLiteral("i_C5_a")),
                   *sub(symbol("Vs"), symbol("V2"))));
        assert(eq(*add(resolved.at(QStringLiteral("i_C5_a")), resolved.at(QStringLiteral("i_C4_a"))),
                   *symbol("Vs")));
    }
    {
        std::unordered_map<QString, RCP<const Basic>> reps;
        reps[QStringLiteral("V1")] = symbol("Vs1");
        reps[QStringLiteral("i_C5_a")] = sub(symbol("V1"), symbol("V2"));
        const SymEngine::map_basic_basic dots = dependentDotSubstitutionMap(
            reps, {QStringLiteral("V2"), QStringLiteral("Vs1")},
            [](const QString& name) { return name == QStringLiteral("V2"); });
        const RCP<const Basic> v2dot =
            add(div(symbol("Is2"), symbol("C4")),
                mul(div(symbol("C5"), symbol("C4")), dots.at(symOf(QStringLiteral("i_C5_a_dot")))));
        assert(eq(*expand(v2dot),
                   *add(div(symbol("Is2"), symbol("C4")),
                        mul(symbol("C5"), sub(div(symbol("Vs1_dot"), symbol("C4")),
                                              div(dotSym(QStringLiteral("V2")), symbol("C4")))))));
    }
    {
        // V3 = Vs1 - i_L7_a must not substitute into i_L7_dot rhs (same-branch tautology).
        std::unordered_map<QString, RCP<const Basic>> dots;
        dots[QStringLiteral("i_L7")] = add(div(neg(symbol("V3")), symbol("L7")),
                                           div(symbol("Vs1"), symbol("L7")));
        const std::vector<RCP<const Basic>> relations = {
            sub(symbol("i_L7_a"), sub(symbol("Vs1"), symbol("V3")))};
        const auto rejectTautology = [](const QString& targetName,
                                        const RCP<const Basic>& targetExpr,
                                        const RCP<const Basic>& primarySym,
                                        const RCP<const Basic>& solved) {
            if (targetName == QStringLiteral("i_L7") &&
                !SymEngine::eq(*linearCoeffRCP(solved, symbol("i_L7_a")), *integer(0))) {
                return false;
            }
            return !substitutionIsCircular(targetExpr, primarySym, solved);
        };
        eliminateSymbolsInto(dots, relations, {QStringLiteral("V3")},
                             [](const QString&) { return true; }, {}, rejectTautology);
        assert(SymEngine::eq(*dots.at(QStringLiteral("i_L7")),
                             *add(div(neg(symbol("V3")), symbol("L7")),
                                  div(symbol("Vs1"), symbol("L7")))));
    }
    {
        // L7 link: V3 via i_R2_a (not i_L7_a tautology); V4 from L3 companion; L7/L3 coupling.
        std::unordered_map<QString, RCP<const Basic>> dots;
        dots[QStringLiteral("i_L7")] = add(div(neg(symbol("V3")), symbol("L7")),
                                           div(symbol("Vs1"), symbol("L7")));
        const std::vector<RCP<const Basic>> relations = {
            sub(symbol("i_L7_a"), sub(symbol("Vs1"), symbol("V3"))),
            sub(add(add(neg(symbol("Is2")), symbol("i_L7")), div(symbol("V4"), symbol("R2"))),
                div(symbol("V3"), symbol("R2"))),
            sub(symbol("V4"), mul(symbol("L3"), sub(symbol("i_L7_dot"), symbol("Is2_dot")))),
        };
        const auto rejectL7Across = [](const QString& targetName,
                                       const RCP<const Basic>& targetExpr,
                                       const RCP<const Basic>& primarySym,
                                       const RCP<const Basic>& solved) {
            if (targetName == QStringLiteral("i_L7") &&
                !SymEngine::eq(*linearCoeffRCP(solved, symbol("i_L7_a")), *integer(0))) {
                return false;
            }
            return !substitutionIsCircular(targetExpr, primarySym, solved);
        };
        eliminateSymbolsInto(dots, relations, {QStringLiteral("V3"), QStringLiteral("V4")},
                             [](const QString&) { return true; }, {}, rejectL7Across);
        resolveStateDotCoupling({QStringLiteral("i_L7")}, dots);
        const RCP<const Basic> expected = div(
            add(symbol("Vs1"),
                add(mul(symbol("L3"), symbol("Is2_dot")),
                    mul(symbol("R2"), sub(symbol("i_L7"), symbol("Is2"))))),
            add(symbol("L7"), symbol("L3")));
        assert(SymEngine::eq(*expand(dots.at(QStringLiteral("i_L7"))), *expand(expected)));
    }
    {
        std::vector<std::vector<RCP<const Basic>>> m = {
            {integer(2), integer(1)},
            {integer(1), integer(1)},
        };
        const std::optional<std::vector<RCP<const Basic>>> x =
            solveLinearSystem(m, {integer(3), integer(2)});
        assert(x);
        assert(eq(*(*x)[0], *integer(1)));
        assert(eq(*(*x)[1], *integer(1)));
    }
    {
        // MIT elemental forms used by computeStateSpaceImpl.
        auto residual = [](const RCP<const Basic>& lhs, const RCP<const Basic>& rhs) {
            return expand(sub(lhs, rhs));
        };
        const RCP<const Basic> m = symbol("m");
        const RCP<const Basic> K = symbol("K");
        const RCP<const Basic> B = symbol("B");
        const RCP<const Basic> L = symbol("L");
        const RCP<const Basic> R = symbol("R");
        const RCP<const Basic> J = symbol("J");
        const RCP<const Basic> v = symbol("v");
        const RCP<const Basic> f = symbol("f");
        const RCP<const Basic> V = symbol("V");
        const RCP<const Basic> i = symbol("i");
        const RCP<const Basic> Omega = symbol("Omega");
        const RCP<const Basic> T = symbol("T");
        const RCP<const Basic> P = symbol("P");
        const RCP<const Basic> Q = symbol("Q");

        assert(eq(*residual(f, mul(m, dotSym(QStringLiteral("v")))), *integer(0)));
        assert(eq(*residual(dotSym(QStringLiteral("f")),
                            mul(elementalConstantCoeff(SystemType::Mechanical, BranchType::T, K), v)),
               *integer(0)));
        assert(eq(*residual(f, mul(elementalConstantCoeff(SystemType::Mechanical, BranchType::D, B), v)),
               *integer(0)));

        assert(eq(*residual(i, mul(symbol("C"), dotSym(QStringLiteral("V")))), *integer(0)));
        assert(eq(*residual(dotSym(QStringLiteral("i")),
                            mul(elementalConstantCoeff(SystemType::Electrical, BranchType::T, L), V)),
               *integer(0)));
        assert(eq(*residual(i, mul(elementalConstantCoeff(SystemType::Electrical, BranchType::D, R), V)),
               *integer(0)));

        assert(eq(*residual(T, mul(J, dotSym(QStringLiteral("Omega")))), *integer(0)));
        assert(eq(*residual(dotSym(QStringLiteral("T")),
                            mul(elementalConstantCoeff(SystemType::MechanicalRotational, BranchType::T, K),
                                Omega)),
               *integer(0)));
        assert(eq(*residual(Q, mul(elementalConstantCoeff(SystemType::Fluid, BranchType::D, symbol("Rp")), P)),
               *integer(0)));
    }
    return true;
}();

}  // namespace

}  // namespace lg
