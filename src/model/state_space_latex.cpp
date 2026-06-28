#include "state_space_detail.h"

#include "elemental_equation.h"

#include <symengine/integer.h>
#include <symengine/printers/latex.h>

#include <QRegularExpression>

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

namespace {

QString latexSymbolBase(const QString& name) {
    if (name.endsWith(QStringLiteral("_across"))) {
        const QString base = name.left(name.size() - QStringLiteral("_across").size());
        return QStringLiteral("%1_{\\mathrm{across}}").arg(base);
    }
    return name;
}

QString fixSyntheticAcrossInLatex(const QString& tex) {
    QString out = tex;
    const QString suffix = QStringLiteral("_across");
    int searchFrom = 0;
    while (true) {
        const int idx = out.indexOf(suffix, searchFrom);
        if (idx < 0) {
            break;
        }
        int start = idx;
        while (start > 0 && out.at(start - 1).isLetterOrNumber()) {
            --start;
        }
        const QString base = out.mid(start, idx - start);
        if (base.isEmpty()) {
            searchFrom = idx + suffix.size();
            continue;
        }
        const QString repl = QStringLiteral("%1_{\\mathrm{across}}").arg(base);
        out.replace(start, idx - start + suffix.size(), repl);
        searchFrom = start + repl.size();
    }
    return out;
}

}  // namespace

QString latexMathSymbol(const QString& name) {
    if (name.endsWith(QStringLiteral("_dot"))) {
        return QStringLiteral("\\dot{%1}").arg(latexMathSymbol(name.left(name.size() - 4)));
    }
    return latexSymbolBase(name);
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
        text.replace(QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(input))),
                     label);
        text.replace(dotName(input), inputDerivativeDisplay(input, label));
    }
    return text;
}

QString latexCoeff(const RCP<const Basic>& coeff) {
    if (eq(*coeff, *integer(0))) {
        return QStringLiteral("0");
    }
    return fixSyntheticAcrossInLatex(QString::fromStdString(SymEngine::latex(*coeff)));
}

QString latexBmatrix(const std::vector<QStringList>& rows) {
    if (rows.empty()) {
        return QStringLiteral("\\left[\\begin{array}{c}\\end{array}\\right]");
    }
    int cols = 0;
    for (const QStringList& row : rows) {
        if (row.size() > cols) {
            cols = row.size();
        }
    }
    QStringList latexRows;
    latexRows.reserve(static_cast<int>(rows.size()));
    for (const QStringList& row : rows) {
        latexRows.push_back(row.join(QStringLiteral(" & ")));
    }
    const QString colSpec = QString(cols, QLatin1Char('c'));
    return QStringLiteral("\\left[\\begin{array}{%1}%2\\end{array}\\right]")
        .arg(colSpec, latexRows.join(QStringLiteral(" \\\\ ")));
}

QString latexColumnVector(const QStringList& entries) {
    std::vector<QStringList> rows;
    rows.reserve(static_cast<size_t>(entries.size()));
    for (const QString& entry : entries) {
        rows.push_back({entry});
    }
    return latexBmatrix(rows);
}



}  // namespace lg::ss
