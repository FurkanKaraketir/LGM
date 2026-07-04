#include "state_space_detail.h"

#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/pow.h>
#include <symengine/rational.h>
#include <symengine/symbol.h>
#include <symengine/parser.h>
#include <symengine/subs.h>

#include <QDebug>

#include <cassert>
#include <stdexcept>

namespace lg::ss {

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

void ssLog(const QString& stage, const QString& detail) {
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

QString matlabCoeff(const RCP<const Basic>& coeff) {
    if (eq(*coeff, *integer(0))) {
        return QStringLiteral("0");
    }
    return exprText(coeff).replace(QStringLiteral("**"), QStringLiteral("^"));
}

void collectExprSymbols(const RCP<const Basic>& expr, std::unordered_set<QString>& out) {
    using SymEngine::Add;
    using SymEngine::Integer;
    using SymEngine::Mul;
    using SymEngine::Pow;
    using SymEngine::Rational;
    using SymEngine::Symbol;
    using SymEngine::down_cast;
    if (SymEngine::is_a<Symbol>(*expr)) {
        out.insert(QString::fromStdString(down_cast<const Symbol&>(*expr).get_name()));
        return;
    }
    if (SymEngine::is_a<Integer>(*expr) || SymEngine::is_a<Rational>(*expr)) {
        return;
    }
    if (SymEngine::is_a<Add>(*expr)) {
        for (const RCP<const Basic>& arg : down_cast<const Add&>(*expr).get_args()) {
            collectExprSymbols(arg, out);
        }
        return;
    }
    if (SymEngine::is_a<Mul>(*expr)) {
        for (const RCP<const Basic>& arg : down_cast<const Mul&>(*expr).get_args()) {
            collectExprSymbols(arg, out);
        }
        return;
    }
    if (SymEngine::is_a<Pow>(*expr)) {
        const Pow& powExpr = down_cast<const Pow&>(*expr);
        collectExprSymbols(powExpr.get_base(), out);
        collectExprSymbols(powExpr.get_exp(), out);
    }
}

QString dotName(const QString& base) {
    return base + QStringLiteral("_dot");
}

RCP<const Basic> dotSym(const QString& base) { return symOf(dotName(base)); }

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
    // ponytail: bounded fixpoint; mutual deps can expand without ever stabilizing
    const size_t maxPasses = replacements.size() + 1;
    for (size_t pass = 0; pass < maxPasses; ++pass) {
        bool changed = false;
        for (auto& [name, expr] : replacements) {
            SymEngine::map_basic_basic map = substitutionMap(replacements);
            if (const std::optional<RCP<const Basic>> self = trySymOf(name)) {
                map.erase(*self);
            }
            const RCP<const Basic> next = expand(subs(expr, map));
            if (!eq(*next, *expr)) {
                expr = next;
                changed = true;
            }
        }
        if (!changed) {
            break;
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

std::optional<std::unordered_map<QString, RCP<const Basic>>> solveLinearSubsystem(
    const std::vector<RCP<const Basic>>& relations, const std::vector<QString>& unknowns,
    const std::vector<RCP<const Basic>>& knownVars) {
    if (unknowns.empty()) {
        return std::unordered_map<QString, RCP<const Basic>>{};
    }

    std::vector<RCP<const Basic>> unknownSyms;
    unknownSyms.reserve(unknowns.size());
    for (const QString& name : unknowns) {
        unknownSyms.push_back(symOf(name));
    }
    std::vector<RCP<const Basic>> allVars = unknownSyms;
    allVars.insert(allVars.end(), knownVars.begin(), knownVars.end());

    struct Row {
        std::vector<RCP<const Basic>> coeffs;
        RCP<const Basic> rhs;
    };
    std::vector<Row> linearRows;
    linearRows.reserve(relations.size());
    for (const RCP<const Basic>& relation : relations) {
        const RCP<const Basic> expr = expand(relation);
        if (!eq(*linearRemainder(expr, allVars), *integer(0))) {
            continue;
        }
        Row row;
        row.coeffs.reserve(unknowns.size());
        for (const RCP<const Basic>& unknownSym : unknownSyms) {
            row.coeffs.push_back(linearCoeffRCP(expr, unknownSym));
        }
        row.rhs = neg(linearRemainder(expr, unknownSyms));
        linearRows.push_back(std::move(row));
    }

    const size_t n = unknowns.size();
    if (linearRows.size() < n) {
        return std::nullopt;
    }

    std::vector<std::vector<RCP<const Basic>>> matrix;
    matrix.reserve(linearRows.size());
    std::vector<RCP<const Basic>> rhs;
    rhs.reserve(linearRows.size());
    for (const Row& row : linearRows) {
        matrix.push_back(row.coeffs);
        rhs.push_back(row.rhs);
    }

    const size_t m = matrix.size();
    size_t pivotRow = 0;
    for (size_t col = 0; col < n && pivotRow < m; ++col) {
        size_t best = pivotRow;
        for (size_t r = pivotRow + 1; r < m; ++r) {
            if (eq(*matrix[best][col], *integer(0)) && !eq(*matrix[r][col], *integer(0))) {
                best = r;
            }
        }
        if (eq(*matrix[best][col], *integer(0))) {
            return std::nullopt;
        }
        if (best != pivotRow) {
            std::swap(matrix[best], matrix[pivotRow]);
            std::swap(rhs[best], rhs[pivotRow]);
        }
        for (size_t r = 0; r < m; ++r) {
            if (r == pivotRow || eq(*matrix[r][col], *integer(0))) {
                continue;
            }
            const RCP<const Basic> factor = div(matrix[r][col], matrix[pivotRow][col]);
            for (size_t k = col; k < n; ++k) {
                matrix[r][k] = expand(sub(matrix[r][k], mul(factor, matrix[pivotRow][k])));
            }
            rhs[r] = expand(sub(rhs[r], mul(factor, rhs[pivotRow])));
        }
        ++pivotRow;
    }
    if (pivotRow != n) {
        return std::nullopt;
    }
    for (size_t r = pivotRow; r < m; ++r) {
        bool allZero = true;
        for (size_t c = 0; c < n; ++c) {
            if (!eq(*matrix[r][c], *integer(0))) {
                allZero = false;
                break;
            }
        }
        if (allZero && !eq(*rhs[r], *integer(0))) {
            return std::nullopt;
        }
    }

    std::vector<RCP<const Basic>> solution(n);
    for (int r = static_cast<int>(n) - 1; r >= 0; --r) {
        RCP<const Basic> sum = rhs[static_cast<size_t>(r)];
        for (size_t c = static_cast<size_t>(r) + 1; c < n; ++c) {
            sum = expand(sub(sum, mul(matrix[static_cast<size_t>(r)][c], solution[c])));
        }
        if (eq(*matrix[static_cast<size_t>(r)][static_cast<size_t>(r)], *integer(0))) {
            return std::nullopt;
        }
        solution[static_cast<size_t>(r)] =
            div(sum, matrix[static_cast<size_t>(r)][static_cast<size_t>(r)]);
    }

    std::unordered_map<QString, RCP<const Basic>> out;
    for (size_t i = 0; i < n; ++i) {
        out[unknowns[i]] = expand(solution[i]);
    }
    return out;
}

std::unordered_set<QString> expandCoupledAlgebraicUnknowns(
    const std::unordered_set<QString>& seeds, const std::vector<RCP<const Basic>>& relations,
    const std::function<bool(const QString&)>& isAlgebraicUnknown) {
    std::unordered_set<QString> unknowns = seeds;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const RCP<const Basic>& relation : relations) {
            std::unordered_set<QString> relSyms;
            collectExprSymbols(relation, relSyms);
            bool touches = false;
            for (const QString& sym : relSyms) {
                if (unknowns.count(sym) != 0) {
                    touches = true;
                    break;
                }
            }
            if (!touches) {
                continue;
            }
            for (const QString& sym : relSyms) {
                if (isAlgebraicUnknown(sym) && unknowns.insert(sym).second) {
                    changed = true;
                }
            }
        }
    }
    return unknowns;
}

RCP<const Basic> rewriteForSyntheticAcrossStateDot(
    const RCP<const Basic>& equation, const BranchItem& branch, const QString& stateSymbol,
    const SymEngine::map_basic_basic& /*valueSubMap*/,
    const std::vector<QString>& /*timedSymbols*/) {
    const QString tautology = branchTautologyAcrossSymbol(branch);
    if (tautology.isEmpty() || stateSymbol != tautology) {
        return equation;
    }
    const RCP<const Basic> stateDot = dotSym(stateSymbol);

    const QString acrossText = branchAcrossVariableText(branch).trimmed();
    const int dash = acrossText.indexOf(QStringLiteral(" - "));
    if (dash > 0) {
        const QString hiName = acrossText.left(dash).trimmed();
        const QString loName = acrossText.mid(dash + 3).trimmed();
        if (!isValidVariableSymbol(hiName) || !isValidVariableSymbol(loName)) {
            return equation;
        }
        // ponytail: valueSubMap rewrites node values (V1→Vs1) but elemental dots stay V1_dot
        const RCP<const Basic> hiDot = dotSym(hiName);
        const RCP<const Basic> loDot = dotSym(loName);
        const RCP<const Basic> acrossDot = sub(hiDot, loDot);
        const RCP<const Basic> cHi = linearCoeffRCP(equation, hiDot);
        const RCP<const Basic> cLo = linearCoeffRCP(equation, loDot);
        if (eq(*expand(add(cHi, cLo)), *integer(0))) {
            return expand(sub(equation, sub(mul(cHi, acrossDot), mul(cHi, stateDot))));
        }
    } else if (isValidVariableSymbol(acrossText)) {
        const RCP<const Basic> nodeDot = dotSym(acrossText);
        const RCP<const Basic> coeff = linearCoeffRCP(equation, nodeDot);
        if (!eq(*coeff, *integer(0))) {
            return expand(sub(equation, sub(mul(coeff, nodeDot), mul(coeff, stateDot))));
        }
    }
    return equation;
}

void resolveStateDotCoupling(const std::vector<QString>& stateSymbols,
                             std::unordered_map<QString, RCP<const Basic>>& stateDots) {
    const size_t n = stateSymbols.size();
    if (n == 0) {
        return;
    }
    ssLog(QStringLiteral("coupling"), QStringLiteral("start n=%1").arg(n));
    std::vector<RCP<const Basic>> dotVars;
    dotVars.reserve(n);
    for (const QString& symbolName : stateSymbols) {
        dotVars.push_back(dotSym(symbolName));
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
        if (eq(*t_ii, *integer(1))) {
            ssLog(QStringLiteral("coupling_degenerate"),
                  QStringLiteral("%1_dot is underdetermined (coefficient 1 on own derivative)")
                      .arg(stateSymbols[i]));
            throw std::runtime_error("degenerate state derivative equation");
        }
        const RCP<const Basic> rhs = linearRemainder(expr, dotVars);
        stateDots[stateSymbols[i]] = expand(div(rhs, sub(integer(1), t_ii)));
        ssLog(QStringLiteral("coupling_decouple"),
              QStringLiteral("%1_dot = %2")
                  .arg(stateSymbols[i], exprText(stateDots[stateSymbols[i]])));
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
        ssLog(QStringLiteral("coupling"), QStringLiteral("end (all decoupled)"));
        return;
    }

    const size_t m = coupled.size();
    {
        QStringList names;
        for (size_t i : coupled) {
            names.push_back(stateSymbols[i]);
        }
        ssLog(QStringLiteral("coupling"),
              QStringLiteral("solving coupled block size=%1: [%2]").arg(m).arg(names.join(QStringLiteral(", "))));
    }
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
        ssLog(QStringLiteral("coupling_solved"),
              QStringLiteral("%1_dot = %2")
                  .arg(stateSymbols[coupled[ri]], exprText((*solved)[ri])));
    }
    ssLog(QStringLiteral("coupling"), QStringLiteral("end"));
}

namespace {

void solveLinearSubsystemSelfCheck() {
    // ponytail: node-3 {V3, i_R3} coupled pair from circuit with 2 inputs
    const RCP<const Basic> i_R3 = symOf(QStringLiteral("i_R3"));
    const RCP<const Basic> V3 = symOf(QStringLiteral("V3"));
    const RCP<const Basic> V4 = symOf(QStringLiteral("V4"));
    const RCP<const Basic> i_L = symOf(QStringLiteral("i_L"));
    const RCP<const Basic> Vs1 = symOf(QStringLiteral("Vs1"));
    const RCP<const Basic> R1 = symOf(QStringLiteral("R1"));
    const RCP<const Basic> R3 = symOf(QStringLiteral("R3"));
    const std::vector<RCP<const Basic>> relations = {
        expand(sub(i_R3, sub(div(V3, R3), div(V4, R3)))),
        expand(sub(add(i_L, i_R3), sub(div(Vs1, R1), div(V3, R1)))),
    };
    const std::vector<QString> unknowns = {QStringLiteral("V3"), QStringLiteral("i_R3")};
    const std::vector<RCP<const Basic>> knownVars = {V4, i_L, Vs1, R1, R3};
    const std::optional<std::unordered_map<QString, RCP<const Basic>>> solved =
        solveLinearSubsystem(relations, unknowns, knownVars);
    assert(solved);
    assert(eq(*linearCoeffRCP(solved->at(QStringLiteral("V3")), V3), *integer(0)));
    assert(eq(*linearCoeffRCP(solved->at(QStringLiteral("i_R3")), i_R3), *integer(0)));
}

const bool kSolveLinearSubsystemSelfCheck = (solveLinearSubsystemSelfCheck(), true);

}  // namespace

}  // namespace lg::ss
