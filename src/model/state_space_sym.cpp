#include "state_space_detail.h"

#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/functions.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/pow.h>
#include <symengine/rational.h>
#include <symengine/symbol.h>
#include <symengine/parser.h>
#include <symengine/subs.h>

#include <QDebug>

#include <algorithm>
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

bool isIdenticallyZero(const RCP<const Basic>& expr) {
    RCP<const Basic> numer;
    RCP<const Basic> denom;
    SymEngine::as_numer_denom(expand(expr), SymEngine::outArg(numer), SymEngine::outArg(denom));
    return eq(*expand(numer), *integer(0));
}

RCP<const Basic> simplifyRational(const RCP<const Basic>& expr) {
    RCP<const Basic> numer;
    RCP<const Basic> denom;
    SymEngine::as_numer_denom(expand(expr), SymEngine::outArg(numer), SymEngine::outArg(denom));
    numer = expand(numer);
    denom = expand(denom);
    // Flip a leading minus out of the denominator: (a + b), not (-a - b).
    if (SymEngine::could_extract_minus(*denom)) {
        numer = expand(neg(numer));
        denom = expand(neg(denom));
    }
    if (!SymEngine::is_a<SymEngine::Add>(*numer)) {
        return div(numer, denom);
    }
    // Keep a sum numerator as separate terms over the denominator: -b/m - R/m, not (-b - R)/m.
    RCP<const Basic> sum = integer(0);
    for (const RCP<const Basic>& term : numer->get_args()) {
        sum = add(sum, div(term, denom));
    }
    return sum;
}

LinearSolveResult solveLinearTargets(std::vector<RCP<const Basic>> equations,
                                     const std::unordered_set<QString>& unknowns,
                                     const std::vector<QString>& targets) {
    LinearSolveResult out;
    const std::unordered_set<QString> targetSet(targets.begin(), targets.end());
    const auto unknownsIn = [&](const RCP<const Basic>& expr) {
        std::unordered_set<QString> symbols;
        collectExprSymbols(expr, symbols);
        std::vector<QString> found;
        for (const QString& symbolName : symbols) {
            if (unknowns.count(symbolName) != 0) {
                found.push_back(symbolName);
            }
        }
        std::sort(found.begin(), found.end());
        return found;
    };

    // Eliminate non-targets first so the equations left over are exactly the projection onto
    // the targets; a target is determined iff that projection pins it down.
    std::vector<std::pair<QString, RCP<const Basic>>> pivots;
    for (const bool pivotTargets : {false, true}) {
        while (true) {
            int bestEq = -1;
            QString bestVar;
            RCP<const Basic> bestCoeff;
            size_t bestCount = 0;
            for (size_t i = 0; i < equations.size(); ++i) {
                const std::vector<QString> vars = unknownsIn(equations[i]);
                if (bestEq >= 0 && SymEngine::is_a_Number(*bestCoeff) && vars.size() >= bestCount) {
                    continue;
                }
                for (const QString& var : vars) {
                    if ((targetSet.count(var) != 0) != pivotTargets) {
                        continue;
                    }
                    const RCP<const Basic> coeff = linearCoeffRCP(equations[i], symOf(var));
                    if (isIdenticallyZero(coeff)) {
                        continue;
                    }
                    // Prefer numeric pivots in short equations: keeps expressions small.
                    const bool numeric = SymEngine::is_a_Number(*coeff);
                    if (bestEq < 0 || (numeric && !SymEngine::is_a_Number(*bestCoeff)) ||
                        (numeric == SymEngine::is_a_Number(*bestCoeff) && vars.size() < bestCount)) {
                        bestEq = static_cast<int>(i);
                        bestVar = var;
                        bestCoeff = coeff;
                        bestCount = vars.size();
                    }
                    if (numeric) {
                        break;
                    }
                }
            }
            if (bestEq < 0) {
                break;
            }
            const RCP<const Basic> var = symOf(bestVar);
            const RCP<const Basic> rest = expand(sub(equations[static_cast<size_t>(bestEq)],
                                                     mul(bestCoeff, var)));
            const RCP<const Basic> solved = expand(div(neg(rest), bestCoeff));
            equations.erase(equations.begin() + bestEq);
            for (RCP<const Basic>& equation : equations) {
                equation = expand(subs(equation, {{var, solved}}));
            }
            pivots.emplace_back(bestVar, solved);
        }
    }
    for (const RCP<const Basic>& equation : equations) {
        if (unknownsIn(equation).empty() && !isIdenticallyZero(equation)) {
            out.inconsistent = true;
            return out;
        }
    }

    SymEngine::map_basic_basic back;
    for (auto it = pivots.rbegin(); it != pivots.rend(); ++it) {
        const RCP<const Basic> value = expand(subs(it->second, back));
        back[symOf(it->first)] = value;
        if (targetSet.count(it->first) != 0 && unknownsIn(value).empty()) {
            out.values[it->first] = value;
        }
    }
    return out;
}

void solveLinearTargetsSelfCheck() {
    // Two parallel capacitors fed by a current source: only the shared node rate is determined.
    const RCP<const Basic> u = symOf(QStringLiteral("u"));
    const RCP<const Basic> f1 = symOf(QStringLiteral("f1"));
    const RCP<const Basic> f2 = symOf(QStringLiteral("f2"));
    const RCP<const Basic> xDot = symOf(QStringLiteral("x_dot"));
    const RCP<const Basic> c1 = symOf(QStringLiteral("C1"));
    const RCP<const Basic> c2 = symOf(QStringLiteral("C2"));
    const LinearSolveResult solved = solveLinearTargets(
        {sub(add(f1, f2), u), sub(f1, mul(c1, xDot)), sub(f2, mul(c2, xDot))},
        {QStringLiteral("f1"), QStringLiteral("f2"), QStringLiteral("x_dot")},
        {QStringLiteral("x_dot")});
    assert(!solved.inconsistent);
    assert(isIdenticallyZero(sub(solved.values.at(QStringLiteral("x_dot")), div(u, add(c1, c2)))));
    // Undetermined target (free split) and inconsistency are both reported.
    assert(solveLinearTargets({sub(add(f1, f2), u)}, {QStringLiteral("f1"), QStringLiteral("f2")},
                              {QStringLiteral("f1")})
               .values.empty());
    assert(solveLinearTargets({sub(f1, u), f1}, {QStringLiteral("f1")}, {}).inconsistent);
    assert(isIdenticallyZero(sub(div(c1, add(c1, c2)), sub(integer(1), div(c2, add(c1, c2))))));
}

}  // namespace lg::ss
