#pragma once

#include "canvas.h"

#include <symengine/basic.h>

#include <optional>

#include <QString>

namespace lg {

bool parseElementConstant(const QString& text, SymEngine::RCP<const SymEngine::Basic>& out);

bool isValidVariableSymbol(const QString& text);

SymEngine::RCP<const SymEngine::Basic> branchElementConstantExpr(const BranchItem& branch);

// MIT linear graph: mechanical uses k in T/D numerators; other domains use 1/k for T/D.
bool elementalConstantInNumerator(SystemType systemType, BranchType type);

SymEngine::RCP<const SymEngine::Basic> elementalConstantCoeff(SystemType systemType, BranchType type,
                                                                const SymEngine::RCP<const SymEngine::Basic>& k);

QString elementalEquationText(BranchType type, SystemType systemType, bool passive, const QString& constantText);

// R/L/C (and domain analogs) imply D/T/A when the branch is still at default A-type.
std::optional<BranchType> inferPassiveBranchType(const QString& constant, SystemType systemType);

BranchType effectivePassiveBranchType(const BranchItem& branch);

SystemType branchSystemType(const BranchItem& branch);

QString systemTypeLabel(SystemType systemType);

QString branchAcrossVariableText(const BranchItem& branch);

QString defaultNodeAcrossName(int id);

QString defaultNodeAcrossName(int id, SystemType systemType);

QString defaultPassiveThroughName(int id);

QString defaultPassiveThroughName(int id, SystemType systemType);

QString throughNameFromConstant(const QString& constant, int fallbackId, SystemType systemType);

QString defaultActiveThroughName(int id);

bool isAutoThroughName(const QString& name);

bool isAutoPassiveThroughName(const QString& name);

bool isAutoPassiveThroughName(const BranchItem& branch);

int branchNumericId(const BranchItem& branch);

int branchSourceInputId(const BranchItem& branch);

int parseSourceInputIdFromName(const QString& name);

QString branchFlowSymbol(const BranchItem& branch);

QString branchAcrossSymbol(const BranchItem& branch);

bool usesSyntheticAcrossSymbol(const BranchItem& branch);

std::optional<SymEngine::RCP<const SymEngine::Basic>> branchAcrossExpression(
    const BranchItem& branch);

bool isTwoPortInternalBranch(const BranchItem& branch);

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

QString twoPortElementalEquationText(TwoPortKind kind, const QString& modulus);

}  // namespace lg
