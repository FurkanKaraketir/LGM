#include "state_space_context.h"
#include "state_space_reduce.h"

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

bool ssAssembleOutputs(
    StateSpaceContext& ctx,
    const std::unordered_map<QString, ss::RCP<const SymEngine::Basic>>& stateDots) {
    if (ctx.requestedOutputs.isEmpty()) {
        return true;
    }
    if (!ctx.reductionRelations) {
        ctx.result.status = StateSpaceResult::Status::SymbolicError;
        ctx.result.message = QStringLiteral("Output assembly requires state-space reduction relations.");
        ss::ssLog(QStringLiteral("error"), ctx.result.message);
        return false;
    }

    using SymEngine::eq;
    using SymEngine::expand;
    using SymEngine::integer;
    using SymEngine::subs;
    using SymEngine::sub;

    const SymEngine::map_basic_basic valueSubMap =
        ss::substitutionMap(ss::resolveReplacements(ctx.replacements));
    const auto isInputLike = [&](const QString& symbolName) {
        if (ctx.result.inputs.contains(symbolName)) {
            return true;
        }
        for (const QString& input : ctx.result.inputs) {
            if (symbolName == ss::dotName(input)) {
                return true;
            }
        }
        return false;
    };
    const SymEngine::map_basic_basic dependentDotMap =
        ss::dependentDotSubstitutionMap(ctx.replacements, ctx.timedSymbols,
                                          [&](const QString& symbol) {
                                              return ctx.isPrimaryState(symbol);
                                          });
    const auto isStateDotLike = [&](const QString& symbolName) {
        if (!symbolName.endsWith(QStringLiteral("_dot"))) {
            return false;
        }
        return ctx.isPrimaryState(symbolName.left(symbolName.size() - 4));
    };

    const std::vector<QString> nodeAcrossSymbols = ss::collectNodeAcrossSymbols(ctx.nodes);
    const auto canEliminate = [&](const QString& candidate) {
        return !candidate.isEmpty() && candidate != QStringLiteral("0") &&
               isValidVariableSymbol(candidate) && !ctx.isPrimaryState(candidate) &&
               !isInputLike(candidate) && !isStateDotLike(candidate);
    };

    const std::unordered_map<QString, ss::RCP<const SymEngine::Basic>> resolved =
        ss::resolveReplacements(ctx.replacements);

    const auto branchExprForOutputSymbol = [&](const QString& symbol)
        -> std::optional<ss::RCP<const SymEngine::Basic>> {
        for (BranchItem* branch : ctx.branches) {
            if (!branch) {
                continue;
            }
            if (branchThroughSymbol(*branch) == symbol) {
                if (branch->isActive() && branch->branchType() == BranchType::T) {
                    return ss::symOf(branchSourceInputSymbol(*branch));
                }
                if (branch->isActive() && branch->branchType() == BranchType::A) {
                    return ss::symOf(branchSourceInputSymbol(*branch));
                }
            }
            if (branchAcrossSymbol(*branch) == symbol) {
                return branchNodeAcrossExpr(*branch);
            }
        }
        return std::nullopt;
    };

    const auto seedOutputExpr = [&](const QString& out) -> ss::RCP<const SymEngine::Basic> {
        const auto it = resolved.find(out);
        if (it != resolved.end()) {
            return expand(it->second);
        }
        if (const std::optional<ss::RCP<const SymEngine::Basic>> branchExpr =
                branchExprForOutputSymbol(out)) {
            return expand(*branchExpr);
        }
        const ss::RCP<const SymEngine::Basic> outSym = ss::symOf(out);
        for (const BranchElemental& elemental : ctx.elementalEquations) {
            const ss::RCP<const SymEngine::Basic> relation =
                expand(subs(elemental.expr, valueSubMap));
            if (const std::optional<ss::RCP<const SymEngine::Basic>> solved =
                    ss::solveLinearFor(relation, outSym)) {
                return expand(*solved);
            }
        }
        for (const auto& [name, expr] : resolved) {
            if (name == out || !isValidVariableSymbol(name)) {
                continue;
            }
            const ss::RCP<const SymEngine::Basic> relation =
                expand(sub(ss::symOf(name), expr));
            if (const std::optional<ss::RCP<const SymEngine::Basic>> solved =
                    ss::solveLinearFor(relation, outSym)) {
                return expand(*solved);
            }
        }
        return expand(outSym);
    };

    SymEngine::map_basic_basic stateDotMap;
    for (const ComputedState& state : ctx.computedStates) {
        stateDotMap[ss::dotSym(state.symbol)] = stateDots.at(state.symbol);
    }

    const auto applyOutputSubs = [&](ss::RCP<const SymEngine::Basic> expr) {
        expr = expand(subs(expr, dependentDotMap));
        expr = expand(subs(expr, valueSubMap));
        return expand(subs(expr, stateDotMap));
    };

    const auto acceptOutputSubstitution = [&](const QString& out) {
        return [&](const QString& targetName, const ss::RCP<const SymEngine::Basic>& targetExpr,
                   const ss::RCP<const SymEngine::Basic>& primarySym,
                   const ss::RCP<const SymEngine::Basic>& solved) {
            (void)targetName;
            (void)targetExpr;
            (void)primarySym;
            const std::optional<ss::RCP<const SymEngine::Basic>> outSym = ss::trySymOf(out);
            if (!outSym) {
                return true;
            }
            // ponytail: out-defining constraints are dropped in filterRelationsForOutput; allow
            // eliminating the output symbol itself (e.g. V4 = L3*i_L3_dot when y = V4).
            return eq(*ss::linearCoeffRCP(solved, *outSym), *integer(0));
        };
    };

    std::vector<ss::RCP<const SymEngine::Basic>> linearVars;
    linearVars.reserve(ctx.computedStates.size() +
                       2 * static_cast<size_t>(ctx.result.inputs.size()));
    QStringList xEntries;
    xEntries.reserve(static_cast<int>(ctx.computedStates.size()));
    for (const ComputedState& state : ctx.computedStates) {
        linearVars.push_back(ss::symOf(state.symbol));
        xEntries.push_back(ss::latexMathSymbol(state.symbol));
    }
    QStringList uEntries;
    QStringList uDotEntries;
    for (int i = 0; i < ctx.result.inputs.size(); ++i) {
        const QString& input = ctx.result.inputs.at(i);
        linearVars.push_back(ss::symOf(input));
        linearVars.push_back(ss::symOf(ss::dotName(input)));
        uEntries.push_back(ss::latexMathSymbol(
            i < ctx.result.inputLabels.size() ? ctx.result.inputLabels.at(i) : input));
        uDotEntries.push_back(i < ctx.result.inputLabels.size()
                                  ? ss::latexInputDotLabel(ctx.result.inputLabels.at(i))
                                  : ss::latexMathSymbol(ss::dotName(input)));
    }

    std::vector<QStringList> cRows;
    std::vector<QStringList> dRows;
    std::vector<QStringList> fRows;
    QStringList yEntries;
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

    const ReductionSubs subMaps{valueSubMap, dependentDotMap};

    for (const QString& out : ctx.requestedOutputs) {
        const QString label = outputLabelFor(out);
        ctx.result.outputLabels.push_back(label);

        std::unordered_map<QString, ss::RCP<const SymEngine::Basic>> single{
            {out, seedOutputExpr(out)}};
        const ReductionRelations outputRels =
            filterRelationsForOutput(*ctx.reductionRelations, ctx, out);
        const ReductionOptions reduceOpts{acceptOutputSubstitution(out), false,
                                          QStringLiteral("output_")};
        ssReduceExpressions(ctx, single, outputRels, subMaps, nodeAcrossSymbols, canEliminate,
                            reduceOpts);
        ss::RCP<const SymEngine::Basic> expr = applyOutputSubs(single.at(out));

        const ss::RCP<const SymEngine::Basic> remainder = ss::linearRemainder(expr, linearVars);
        if (!eq(*remainder, *integer(0))) {
            ctx.result.status = StateSpaceResult::Status::SymbolicError;
            ctx.result.message = QStringLiteral("Output %1 has unresolved terms: %2.")
                                     .arg(label, ss::exprText(remainder));
            ss::ssLog(QStringLiteral("error"), ctx.result.message);
            return false;
        }

        yEntries.push_back(ss::latexMathSymbol(label));
        QStringList cRow;
        cRow.reserve(static_cast<int>(ctx.computedStates.size()));
        for (const ComputedState& state : ctx.computedStates) {
            cRow.push_back(ss::latexCoeff(ss::linearCoeffRCP(expr, ss::symOf(state.symbol))));
        }
        cRows.push_back(cRow);

        if (!ctx.result.inputs.isEmpty()) {
            QStringList dRow;
            QStringList fRow;
            dRow.reserve(ctx.result.inputs.size());
            fRow.reserve(ctx.result.inputs.size());
            for (const QString& input : ctx.result.inputs) {
                dRow.push_back(ss::latexCoeff(ss::linearCoeffRCP(expr, ss::symOf(input))));
                fRow.push_back(
                    ss::latexCoeff(ss::linearCoeffRCP(expr, ss::symOf(ss::dotName(input)))));
            }
            dRows.push_back(dRow);
            fRows.push_back(fRow);
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
    ss::ssLog(QStringLiteral("output_matrix_form"), latex);
    return true;
}

}  // namespace lg
