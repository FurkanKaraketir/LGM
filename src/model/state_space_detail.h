#pragma once

#include "normal_tree.h"

#include <symengine/basic.h>

#include <functional>
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
QString dotName(const QString& base);
RCP<const Basic> dotSym(const QString& base);

// --- linear algebra ---

std::optional<RCP<const Basic>> solveLinearFor(const RCP<const Basic>& equation,
                                               const RCP<const Basic>& unknown);
SymEngine::map_basic_basic substitutionMap(
    const std::unordered_map<QString, RCP<const Basic>>& replacements);
std::unordered_map<QString, RCP<const Basic>> resolveReplacements(
    std::unordered_map<QString, RCP<const Basic>> replacements);
RCP<const Basic> linearTimeDerivative(const RCP<const Basic>& expr,
                                        const std::vector<QString>& timedSymbols);
SymEngine::map_basic_basic extendedSubstitutionMap(
    const std::unordered_map<QString, RCP<const Basic>>& replacements,
    const std::vector<QString>& timedSymbols);
SymEngine::map_basic_basic dependentDotSubstitutionMap(
    const std::unordered_map<QString, RCP<const Basic>>& replacements,
    const std::vector<QString>& timedSymbols,
    const std::function<bool(const QString&)>& isPrimaryState);
RCP<const Basic> linearCoeffRCP(const RCP<const Basic>& expr, const RCP<const Basic>& variable);
RCP<const Basic> linearRemainder(const RCP<const Basic>& expr,
                                 const std::vector<RCP<const Basic>>& linearVars);
std::optional<std::vector<RCP<const Basic>>> solveLinearSystem(
    std::vector<std::vector<RCP<const Basic>>> matrix, std::vector<RCP<const Basic>> rhs);
void resolveStateDotCoupling(const std::vector<QString>& stateSymbols,
                             std::unordered_map<QString, RCP<const Basic>>& stateDots);
RCP<const Basic> rewriteForSyntheticAcrossStateDot(
    const RCP<const Basic>& equation, const BranchItem& branch, const QString& stateSymbol,
    const SymEngine::map_basic_basic& valueSubMap, const std::vector<QString>& timedSymbols);

// --- graph ---

QString storageStateSymbol(const BranchItem& branch, bool inTree);
bool isStateBranch(const NormalTreeResult& tree, BranchItem* branch,
                   const std::vector<BranchItem*>& branches,
                   const std::vector<TwoPortItem*>& twoPorts = {});
std::unordered_set<NodeItem*> reachableTreeNodes(NodeItem* start,
                                                  const std::vector<BranchItem*>& treeBranches,
                                                  BranchItem* excludedTwig);
std::optional<std::vector<BranchItem*>> treePathBetween(
    NodeItem* start, NodeItem* goal, const std::vector<BranchItem*>& treeBranches);
RCP<const Basic> signedAcross(BranchItem* branch, NodeItem* from, NodeItem* to);
RCP<const Basic> signedThrough(BranchItem* branch, NodeItem* from, NodeItem* to);
// ponytail: omitFromContinuityCut drops duplicate port bonds; parallel user flow counted once.
RCP<const Basic> signedThroughForContinuityCut(BranchItem* branch, NodeItem* from, NodeItem* to,
                                               BranchItem* twig,
                                               const std::vector<BranchItem*>& branches,
                                               const std::vector<TwoPortItem*>& twoPorts);
std::vector<QString> collectNodeAcrossSymbols(const std::vector<NodeItem*>& nodes);

// Chains port-flow replacements to a co-tree compliance through-state (e.g. f_K).
std::optional<RCP<const Basic>> composeReflectedCoTreeForce(
    const QString& startPortFlow, const QString& complianceFlow,
    const std::unordered_map<QString, RCP<const Basic>>& replacements,
    const std::vector<QString>& branchThroughSymbols);

// Effort-node path between two ports through transformers; returns TF modulus product.
std::optional<RCP<const Basic>> transformerModulusProductBetween(
    const NodeItem* fromAcross, const NodeItem* toAcross,
    const std::vector<TwoPortItem*>& twoPorts);

// --- elimination ---

bool substitutionIsCircular(const RCP<const Basic>& target, const RCP<const Basic>& primarySym,
                            const RCP<const Basic>& solved);

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
    const SubstitutionFilter& acceptSubstitution = {},
    const QString& phase = {});

QString branchTautologyAcrossSymbol(const BranchItem& branch);

void eliminateBranchSymbolsInto(
    std::unordered_map<QString, RCP<const Basic>>& targets,
    const std::vector<RCP<const Basic>>& relations,
    const std::vector<BranchItem*>& branches,
    const std::unordered_set<BranchItem*>& treeSet,
    const std::function<bool(const QString&)>& canEliminate,
    const SubstitutionFilter& acceptSubstitution = {},
    const QString& phase = {});

std::vector<RCP<const Basic>> constraintRelations(
    const std::unordered_map<QString, RCP<const Basic>>& replacements);

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
