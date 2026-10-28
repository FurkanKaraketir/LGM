#pragma once

#include "normal_tree.h"

#include <symengine/basic.h>

#include <QHashFunctions>

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BranchItem;
class NodeItem;

namespace lg::ss {

using SymEngine::Basic;
using SymEngine::RCP;

// --- logging / symbols ---

void ssLog(const QString& stage, const QString& detail = {});
void ssLogExprs(const QString& label, const std::vector<RCP<const Basic>>& exprs);

std::optional<RCP<const Basic>> trySymOf(const QString& name);
RCP<const Basic> symOf(const QString& name);
QString exprText(const RCP<const Basic>& expr);
QString matlabCoeff(const RCP<const Basic>& coeff);
void collectExprSymbols(const RCP<const Basic>& expr, std::unordered_set<QString>& out);
QString dotName(const QString& base);
RCP<const Basic> dotSym(const QString& base);

// --- linear algebra ---

std::optional<RCP<const Basic>> solveLinearFor(const RCP<const Basic>& equation,
                                               const RCP<const Basic>& unknown);
RCP<const Basic> linearTimeDerivative(const RCP<const Basic>& expr,
                                        const std::vector<QString>& timedSymbols);
RCP<const Basic> linearCoeffRCP(const RCP<const Basic>& expr, const RCP<const Basic>& variable);
RCP<const Basic> linearRemainder(const RCP<const Basic>& expr,
                                 const std::vector<RCP<const Basic>>& linearVars);
// Zero as a rational function of its symbols (expand() alone misses a/(a+b) + b/(a+b) - 1).
bool isIdenticallyZero(const RCP<const Basic>& expr);
RCP<const Basic> simplifyRational(const RCP<const Basic>& expr);

struct LinearSolveResult {
    bool inconsistent = false;
    std::unordered_map<QString, RCP<const Basic>> values;  // determined targets only
};

// Solves linear equations (each expr = 0) for `targets` in terms of every symbol outside
// `unknowns`. A target missing from `values` is not determined by the equations.
LinearSolveResult solveLinearTargets(std::vector<RCP<const Basic>> equations,
                                     const std::unordered_set<QString>& unknowns,
                                     const std::vector<QString>& targets);
// Assert-based check of solveLinearTargets; run on first use, not at static init (SymEngine globals).
void solveLinearTargetsSelfCheck();

// --- graph ---

std::unordered_set<NodeItem*> reachableTreeNodes(NodeItem* start,
                                                  const std::vector<BranchItem*>& treeBranches,
                                                  BranchItem* excludedTwig);
RCP<const Basic> signedThrough(BranchItem* branch, NodeItem* from, NodeItem* to);
std::vector<QString> collectNodeAcrossSymbols(const std::vector<NodeItem*>& nodes);

// --- latex / display ---

bool matrixEntryIsZero(const QString& entry);
bool matrixIsZero(const std::vector<QStringList>& rows);
QString latexMathSymbol(const QString& name);
QString latexInputDotLabel(const QString& inputLabel);
QString displayExprText(const RCP<const Basic>& expr, const QStringList& inputs,
                        const QStringList& inputLabels);
QString latexCoeff(const RCP<const Basic>& coeff);
QString latexBmatrix(const std::vector<QStringList>& rows);
QString latexColumnVector(const QStringList& entries);

}  // namespace lg::ss
