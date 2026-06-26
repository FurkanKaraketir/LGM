#pragma once

#include "canvas.h"

#include <symengine/basic.h>

#include <optional>

#include <QString>

namespace lg {

bool parseElementConstant(const QString& text, SymEngine::RCP<const SymEngine::Basic>& out);

bool isValidElementConstant(const QString& text);

bool isValidVariableSymbol(const QString& text);

SymEngine::RCP<const SymEngine::Basic> branchElementConstantExpr(const BranchItem& branch);

// MIT linear graph: mechanical uses k in T/D numerators; other domains use 1/k for T/D.
bool elementalConstantInNumerator(SystemType systemType, BranchType type);

SymEngine::RCP<const SymEngine::Basic> elementalConstantCoeff(SystemType systemType, BranchType type,
                                                                const SymEngine::RCP<const SymEngine::Basic>& k);

QString elementalEquationText(BranchType type, SystemType systemType, bool passive, const QString& constantText);

// Prefix heuristic for storage-branch detection (does not set branch type).
std::optional<BranchType> inferPassiveBranchType(const QString& constant, SystemType systemType);

BranchType effectivePassiveBranchType(const BranchItem& branch);

SystemType branchSystemType(const BranchItem& branch);

QString systemTypeLabel(SystemType systemType);

QString branchAcrossVariableText(const BranchItem& branch);

QString defaultNodeAcrossName(int id);

QString defaultNodeAcrossName(int id, SystemType systemType);

QString defaultPassiveThroughName(int id);

QString defaultPassiveThroughName(int id, SystemType systemType);

QString defaultElementConstant(int id, SystemType systemType);

QString throughNameFromConstant(const QString& constant, int fallbackId, SystemType systemType);

QString defaultActiveThroughName(int id);

bool isAutoThroughName(const QString& name);

bool isAutoPassiveThroughName(const QString& name);

bool isAutoPassiveThroughName(const BranchItem& branch);

int branchNumericId(const BranchItem& branch);

int branchSourceInputId(const BranchItem& branch);

int parseSourceInputIdFromName(const QString& name);

QString branchThroughSymbol(const BranchItem& branch);

QString branchAcrossSymbol(const BranchItem& branch);

// Synthetic name when across is a node difference (e.g. C5_across for V1−V2 on C5).
QString syntheticAcrossSymbolName(const BranchItem& branch);

bool usesSyntheticAcrossSymbol(const BranchItem& branch);

std::optional<SymEngine::RCP<const SymEngine::Basic>> branchAcrossExpression(
    const BranchItem& branch);

// Node-across variable (e.g. V1, V1 - V2); falls back to branchAcrossSymbol when not parseable.
SymEngine::RCP<const SymEngine::Basic> branchNodeAcrossExpr(const BranchItem& branch);

bool isTwoPortInternalBranch(const BranchItem& branch);

bool sharesEndpoints(const BranchItem* a, const BranchItem* b);

// True when branch is a user element drawn on the same nodes as a two-port port edge.
bool isExternalBranchOnPortSpan(const BranchItem& branch,
                                const std::vector<BranchItem*>& branches);

// A-type storage (M, J, C, …) on a coupler edge — still a state when in the normal tree.
bool isPortSpanStorageBranch(const BranchItem& branch,
                             const std::vector<BranchItem*>& branches);

// User element on a two-port port span (e.g. inertia on the coupler edge).
BranchItem* externalBranchParallelToPort(const BranchItem* port,
                                           const std::vector<BranchItem*>& branches,
                                           const std::vector<TwoPortItem*>& twoPorts);

// All user branches on the same nodes as a port edge (inertia, source, …).
std::vector<BranchItem*> branchesParallelToPort(
    const BranchItem* port, const std::vector<BranchItem*>& branches,
    const std::vector<TwoPortItem*>& twoPorts);

SymEngine::RCP<const SymEngine::Basic> signedParallelPortSpanFlowSum(
    const BranchItem& port, const std::vector<BranchItem*>& parallels);

// Sum signed through-flows on the port span (KCL), optionally excluding the port itself.
SymEngine::RCP<const SymEngine::Basic> signedSpanJunctionFlowSum(
    const BranchItem& port, const std::vector<BranchItem*>& branches,
    const std::vector<TwoPortItem*>& twoPorts, bool excludePort = true);

bool omitFromContinuityCut(const BranchItem* branch,
                           const std::vector<BranchItem*>& branches,
                           const std::vector<TwoPortItem*>& twoPorts);

// User or port-span twig: elemental + transformer rules replace the cut-sum balance.
bool skipTwigFlowContinuity(const BranchItem& twig,
                            const std::vector<TwoPortItem*>& twoPorts,
                            const std::vector<BranchItem*>& branches);

SymEngine::RCP<const SymEngine::Basic> signedParallelFlowExpr(const BranchItem& fromRef,
                                                                const BranchItem& parallel);

SymEngine::RCP<const SymEngine::Basic> twoPortModulusExpr(const TwoPortItem& twoPort);

QString branchStorageAcrossSymbol(const BranchItem& branch);

QString branchSourceInputSymbol(const BranchItem& branch);

QString branchSourceInputDisplay(const BranchItem& branch);

QString inputDerivativeDisplay(const QString& inputSymbol, const QString& inputLabel);

QString branchSourceInputDotDisplay(const BranchItem& branch);

void applySourceThroughNaming(BranchItem* branch);

void applyConstantThroughNaming(BranchItem* branch);

void applyNodeDomainNaming(NodeItem* node);

QString defaultTwoPortModulus(TwoPortKind kind);

QString twoPortElementalEquationText(TwoPortKind kind, const QString& modulus,
                                     const BranchItem* left = nullptr,
                                     const BranchItem* right = nullptr);

}  // namespace lg
