#include "state_space_detail.h"

#include "elemental_equation.h"

#include <symengine/integer.h>
#include <symengine/printers/latex.h>

#include <cassert>

namespace lg::ss {

using SymEngine::eq;
using SymEngine::integer;

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

namespace {

const bool kLatexSelfCheck = [] {
    assert(latexMathSymbol(QStringLiteral("x_dot")) == QStringLiteral("\\dot{x}"));
    assert(latexCoeff(SymEngine::div(SymEngine::integer(1), SymEngine::symbol("m"))) ==
           QStringLiteral("\\frac{1}{m}"));
    {
        const RCP<const Basic> nested = SymEngine::div(
            SymEngine::integer(-1),
            SymEngine::mul(SymEngine::symbol("L7"),
                           SymEngine::add(SymEngine::integer(1),
                                          SymEngine::div(SymEngine::symbol("L3"),
                                                         SymEngine::symbol("L7")))));
        const QString tex = latexCoeff(nested);
        assert(tex.startsWith(QStringLiteral("\\frac{")));
        assert(!tex.contains(QStringLiteral("\\cdot")));
        assert(tex.count(QLatin1Char('(')) == tex.count(QLatin1Char(')')));
    }
    assert(latexColumnVector({QStringLiteral("a"), QStringLiteral("b")}) ==
           QStringLiteral("\\begin{bmatrix}a \\\\ b\\end{bmatrix}"));
    return true;
}();

}  // namespace

}  // namespace lg::ss
