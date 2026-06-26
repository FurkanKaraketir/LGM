#include "state_space_context.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/subs.h>

#include <algorithm>
#include <unordered_map>

namespace lg {

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
            aRow.reserve(static_cast<int>(ctx.computedStates.size()));
            for (const ComputedState& col : ctx.computedStates) {
                aRow.push_back(ss::latexCoeff(ss::linearCoeffRCP(rhs, ss::symOf(col.symbol))));
            }
            aRows.push_back(aRow);
            if (!ctx.result.inputs.isEmpty()) {
                QStringList bRow;
                QStringList eRow;
                bRow.reserve(ctx.result.inputs.size());
                eRow.reserve(ctx.result.inputs.size());
                for (const QString& input : ctx.result.inputs) {
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
        ss::ssLog(QStringLiteral("matrix_form"), latex);
    }

    ctx.result.status = StateSpaceResult::Status::Ok;
    ctx.result.message = QStringLiteral("State order %1, %2 continuity, %3 compatibility, %4 inputs.")
                         .arg(ctx.computedStates.size())
                         .arg(ctx.result.continuityEquations.size())
                         .arg(ctx.result.compatibilityEquations.size())
                         .arg(ctx.result.inputs.size());
}

}  // namespace lg
