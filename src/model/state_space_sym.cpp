#include "state_space_detail.h"

#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/parser.h>
#include <symengine/subs.h>
#include <symengine/symbol.h>

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

const bool kStateSpaceSelfCheck = [] {
    {
        std::unordered_map<QString, RCP<const Basic>> reps;
        reps[QStringLiteral("A")] = add(symbol("B"), integer(1));
        reps[QStringLiteral("B")] = add(symbol("A"), integer(1));
        const auto resolved = resolveReplacements(reps);
        assert(resolved.size() == 2);
    }
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
    {
        const RCP<const Basic> leadLagAcross = sub(symbol("V1"), symbol("v2"));
        const RCP<const Basic> leadLagAcrossDot =
            linearTimeDerivative(leadLagAcross, {QStringLiteral("V1"), QStringLiteral("v2")});
        assert(eq(*leadLagAcrossDot, *sub(dotSym(QStringLiteral("V1")), dotSym(QStringLiteral("v2")))));
    }
    {
        std::unordered_map<QString, RCP<const Basic>> reps;
        reps[QStringLiteral("C1_a")] = sub(symbol("V1"), symbol("v2"));
        const SymEngine::map_basic_basic map =
            extendedSubstitutionMap(reps, {QStringLiteral("V1"), QStringLiteral("v2")});
        const RCP<const Basic> elem =
            sub(symbol("f1"), mul(symbol("C1"), map.at(symOf(dotName(QStringLiteral("C1_a"))))));
        const RCP<const Basic> expected =
            sub(symbol("f1"), mul(symbol("C1"), linearTimeDerivative(reps.at(QStringLiteral("C1_a")),
                                                                       {QStringLiteral("V1"),
                                                                        QStringLiteral("v2")})));
        assert(eq(*expand(elem), *expand(expected)));
    }
    {
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
        const std::vector<RCP<const Basic>> stateInputVars = {symbol("V2"), symbol("I8")};
        const RCP<const Basic> reduced =
            add(mul(symbol("R1"), symbol("V2")), mul(symbol("C5"), symbol("I8")));
        assert(eq(*linearRemainder(reduced, stateInputVars), *integer(0)));
        const RCP<const Basic> leftover = add(reduced, symbol("i7"));
        assert(eq(*linearRemainder(leftover, stateInputVars), *symbol("i7")));
    }
    {
        std::unordered_map<QString, RCP<const Basic>> reps;
        reps[QStringLiteral("A")] = add(symbol("B"), integer(1));
        reps[QStringLiteral("B")] = add(symbol("A"), integer(1));
        const auto resolved = resolveReplacements(reps);
        assert(resolved.size() == 2);
    }
    {
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

}  // namespace lg::ss
