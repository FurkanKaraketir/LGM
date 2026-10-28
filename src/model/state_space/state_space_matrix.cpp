#include "state_space_context.h"

#include "canvas.h"
#include "elemental_equation.h"
#include "state_space.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/subs.h>
#include <symengine/symbol.h>

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace lg {

namespace {

void trackCoeff(const ss::RCP<const ss::Basic>& coeff, std::unordered_set<QString>& parameters) {
    ss::collectExprSymbols(coeff, parameters);
}

QStringList filterParameters(const std::unordered_set<QString>& symbols, const QStringList& states,
                             const QStringList& inputs) {
    QStringList params;
    params.reserve(static_cast<int>(symbols.size()));
    for (const QString& sym : symbols) {
        if (states.contains(sym) || inputs.contains(sym)) {
            continue;
        }
        if (sym.endsWith(QStringLiteral("_dot"))) {
            const QString base = sym.left(sym.size() - 4);
            if (states.contains(base) || inputs.contains(base)) {
                continue;
            }
        }
        params.push_back(sym);
    }
    params.sort();
    return params;
}

}  // namespace

void ssAssembleMatrix(StateSpaceContext& ctx,
                      const std::unordered_map<QString, ss::RCP<const ss::Basic>>& stateDots) {
    using SymEngine::eq;
    const auto& computedStates = ctx.computedStates;
    auto& result = ctx.result;
    if (!ctx.computedStates.empty()) {
        QStringList xDotEntries;
        QStringList xEntries;
        std::vector<QStringList> aRows;
        std::vector<QStringList> bRows;
        std::vector<QStringList> eRows;
        std::vector<QStringList> aRowsMatlab;
        std::vector<QStringList> bRowsMatlab;
        std::vector<QStringList> eRowsMatlab;
        std::unordered_set<QString> parameterSymbols;
        QStringList uEntries;
        QStringList uDotEntries;
        xDotEntries.reserve(static_cast<int>(ctx.computedStates.size()));
        xEntries.reserve(static_cast<int>(ctx.computedStates.size()));
        aRows.reserve(ctx.computedStates.size());
        for (int i = 0; i < ctx.result.inputs.size(); ++i) {
            const QString& input = ctx.result.inputs.at(i);
            uEntries.push_back(ss::latexMathSymbol(
                i < ctx.result.inputLabels.size() ? ctx.result.inputLabels.at(i) : input));
            uDotEntries.push_back(i < ctx.result.inputLabels.size()
                                      ? ss::latexInputDotLabel(ctx.result.inputLabels.at(i))
                                      : ss::latexMathSymbol(ss::dotName(input)));
        }
        if (!ctx.result.inputs.isEmpty()) {
            bRows.reserve(ctx.computedStates.size());
            eRows.reserve(ctx.computedStates.size());
        }
        for (const ComputedState& state : ctx.computedStates) {
            const ss::RCP<const SymEngine::Basic> rhs = stateDots.at(state.symbol);
            xDotEntries.push_back(ss::latexMathSymbol(ss::dotName(state.symbol)));
            xEntries.push_back(ss::latexMathSymbol(state.symbol));
            QStringList aRow;
            QStringList aRowMatlab;
            aRow.reserve(static_cast<int>(ctx.computedStates.size()));
            aRowMatlab.reserve(static_cast<int>(ctx.computedStates.size()));
            for (const ComputedState& col : ctx.computedStates) {
                const ss::RCP<const SymEngine::Basic> coeff =
                    ss::linearCoeffRCP(rhs, ss::symOf(col.symbol));
                aRow.push_back(ss::latexCoeff(coeff));
                aRowMatlab.push_back(ss::matlabCoeff(coeff));
                trackCoeff(coeff, parameterSymbols);
            }
            aRows.push_back(aRow);
            aRowsMatlab.push_back(aRowMatlab);
            if (!ctx.result.inputs.isEmpty()) {
                QStringList bRow;
                QStringList eRow;
                QStringList bRowMatlab;
                QStringList eRowMatlab;
                bRow.reserve(ctx.result.inputs.size());
                eRow.reserve(ctx.result.inputs.size());
                bRowMatlab.reserve(ctx.result.inputs.size());
                eRowMatlab.reserve(ctx.result.inputs.size());
                for (const QString& input : ctx.result.inputs) {
                    const ss::RCP<const SymEngine::Basic> bCoeff =
                        ss::linearCoeffRCP(rhs, ss::symOf(input));
                    const ss::RCP<const SymEngine::Basic> eCoeff =
                        ss::linearCoeffRCP(rhs, ss::symOf(ss::dotName(input)));
                    bRow.push_back(ss::latexCoeff(bCoeff));
                    eRow.push_back(ss::latexCoeff(eCoeff));
                    bRowMatlab.push_back(ss::matlabCoeff(bCoeff));
                    eRowMatlab.push_back(ss::matlabCoeff(eCoeff));
                    trackCoeff(bCoeff, parameterSymbols);
                    trackCoeff(eCoeff, parameterSymbols);
                }
                bRows.push_back(bRow);
                eRows.push_back(eRow);
                bRowsMatlab.push_back(bRowMatlab);
                eRowsMatlab.push_back(eRowMatlab);
            }
        }
        QString latex = QStringLiteral("$");
        latex += ss::latexColumnVector(xDotEntries);
        latex += QStringLiteral(" = ");
        latex += ss::latexBmatrix(aRows);
        latex += QStringLiteral(" ");
        latex += ss::latexColumnVector(xEntries);
        if (!ctx.result.inputs.isEmpty()) {
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
        ctx.result.matrixForm = latex;
        ctx.result.matrices.A = std::move(aRowsMatlab);
        ctx.result.matrices.B = std::move(bRowsMatlab);
        ctx.result.matrices.E = std::move(eRowsMatlab);
        ctx.result.matrices.parameters =
            filterParameters(parameterSymbols, ctx.result.stateVariables, ctx.result.inputs);
        ss::ssLog(QStringLiteral("matrix_form"), latex);
    }

    ctx.result.status = StateSpaceResult::Status::Ok;
    ctx.result.message = QStringLiteral("State order %1, %2 continuity, %3 compatibility, %4 inputs.")
                         .arg(ctx.computedStates.size())
                         .arg(ctx.result.continuityEquations.size())
                         .arg(ctx.result.compatibilityEquations.size())
                         .arg(ctx.result.inputs.size());
}

bool ssAssembleOutputs(StateSpaceContext& ctx) {
    if (ctx.requestedOutputs.isEmpty()) {
        return true;
    }

    QStringList xEntries;
    xEntries.reserve(static_cast<int>(ctx.computedStates.size()));
    for (const ComputedState& state : ctx.computedStates) {
        xEntries.push_back(ss::latexMathSymbol(state.symbol));
    }
    QStringList uEntries;
    QStringList uDotEntries;
    for (int i = 0; i < ctx.result.inputs.size(); ++i) {
        const QString& input = ctx.result.inputs.at(i);
        uEntries.push_back(ss::latexMathSymbol(
            i < ctx.result.inputLabels.size() ? ctx.result.inputLabels.at(i) : input));
        uDotEntries.push_back(i < ctx.result.inputLabels.size()
                                  ? ss::latexInputDotLabel(ctx.result.inputLabels.at(i))
                                  : ss::latexMathSymbol(ss::dotName(input)));
    }

    std::vector<QStringList> cRows;
    std::vector<QStringList> dRows;
    std::vector<QStringList> fRows;
    std::vector<QStringList> cRowsMatlab;
    std::vector<QStringList> dRowsMatlab;
    std::vector<QStringList> fRowsMatlab;
    QStringList yEntries;
    std::unordered_set<QString> parameterSymbols;
    for (const QString& sym : ctx.result.matrices.parameters) {
        parameterSymbols.insert(sym);
    }
    cRows.reserve(ctx.requestedOutputs.size());
    dRows.reserve(ctx.requestedOutputs.size());
    fRows.reserve(ctx.requestedOutputs.size());
    yEntries.reserve(ctx.requestedOutputs.size());

    const std::vector<GraphOutputVariable> choices =
        collectOutputVariableChoices(ctx.nodes, ctx.branches);
    const auto outputLabelFor = [&](const QString& out) -> QString {
        for (const GraphOutputVariable& choice : choices) {
            if (choice.symbol == out) {
                return choice.label;
            }
        }
        return out;
    };

    ctx.result.outputs = ctx.requestedOutputs;
    ctx.result.outputLabels.reserve(ctx.requestedOutputs.size());

    for (const QString& out : ctx.requestedOutputs) {
        const QString label = outputLabelFor(out);
        ctx.result.outputLabels.push_back(label);

        const ss::RCP<const SymEngine::Basic> expr = ctx.outputExprs.at(out);

        yEntries.push_back(ss::latexMathSymbol(label));
        QStringList cRow;
        QStringList cRowMatlab;
        cRow.reserve(static_cast<int>(ctx.computedStates.size()));
        cRowMatlab.reserve(static_cast<int>(ctx.computedStates.size()));
        for (const ComputedState& state : ctx.computedStates) {
            const ss::RCP<const SymEngine::Basic> coeff =
                ss::linearCoeffRCP(expr, ss::symOf(state.symbol));
            cRow.push_back(ss::latexCoeff(coeff));
            cRowMatlab.push_back(ss::matlabCoeff(coeff));
            trackCoeff(coeff, parameterSymbols);
        }
        cRows.push_back(cRow);
        cRowsMatlab.push_back(cRowMatlab);

        if (!ctx.result.inputs.isEmpty()) {
            QStringList dRow;
            QStringList fRow;
            QStringList dRowMatlab;
            QStringList fRowMatlab;
            dRow.reserve(ctx.result.inputs.size());
            fRow.reserve(ctx.result.inputs.size());
            dRowMatlab.reserve(ctx.result.inputs.size());
            fRowMatlab.reserve(ctx.result.inputs.size());
            for (const QString& input : ctx.result.inputs) {
                const ss::RCP<const SymEngine::Basic> dCoeff =
                    ss::linearCoeffRCP(expr, ss::symOf(input));
                const ss::RCP<const SymEngine::Basic> fCoeff =
                    ss::linearCoeffRCP(expr, ss::symOf(ss::dotName(input)));
                dRow.push_back(ss::latexCoeff(dCoeff));
                fRow.push_back(ss::latexCoeff(fCoeff));
                dRowMatlab.push_back(ss::matlabCoeff(dCoeff));
                fRowMatlab.push_back(ss::matlabCoeff(fCoeff));
                trackCoeff(dCoeff, parameterSymbols);
                trackCoeff(fCoeff, parameterSymbols);
            }
            dRows.push_back(dRow);
            fRows.push_back(fRow);
            dRowsMatlab.push_back(dRowMatlab);
            fRowsMatlab.push_back(fRowMatlab);
        }

        const QString rhs =
            ss::displayExprText(expr, ctx.result.inputs, ctx.result.inputLabels);
        ctx.result.outputEquations.push_back(QStringLiteral("%1 = %2").arg(label, rhs));
        ss::ssLog(QStringLiteral("output_equation"), QStringLiteral("%1 = %2").arg(label, rhs));
    }

    QString latex = QStringLiteral("$");
    latex += ss::latexColumnVector(yEntries);
    latex += QStringLiteral(" = ");
    latex += ss::latexBmatrix(cRows);
    latex += QStringLiteral(" ");
    latex += ss::latexColumnVector(xEntries);
    if (!ctx.result.inputs.isEmpty()) {
        latex += QStringLiteral(" + ");
        latex += ss::latexBmatrix(dRows);
        latex += QStringLiteral(" ");
        latex += ss::latexColumnVector(uEntries);
        if (!ss::matrixIsZero(fRows)) {
            latex += QStringLiteral(" + ");
            latex += ss::latexBmatrix(fRows);
            latex += QStringLiteral(" ");
            latex += ss::latexColumnVector(uDotEntries);
        }
    }
    latex += QStringLiteral("$");
    ctx.result.outputMatrixForm = latex;
    ctx.result.matrices.C = std::move(cRowsMatlab);
    ctx.result.matrices.D = std::move(dRowsMatlab);
    ctx.result.matrices.F = std::move(fRowsMatlab);
    ctx.result.matrices.parameters =
        filterParameters(parameterSymbols, ctx.result.stateVariables, ctx.result.inputs);
    ss::ssLog(QStringLiteral("output_matrix_form"), latex);
    return true;
}

}  // namespace lg
