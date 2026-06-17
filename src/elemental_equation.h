#pragma once

#include "canvas.h"

#include <symengine/basic.h>

#include <QString>

namespace lg {

bool parseElementConstant(const QString& text, SymEngine::RCP<const SymEngine::Basic>& out);

bool isValidVariableSymbol(const QString& text);

SymEngine::RCP<const SymEngine::Basic> branchElementConstantExpr(const BranchItem& branch);

QString elementalEquationText(BranchType type, bool passive, const QString& constantText);

QString branchAcrossVariableText(const BranchItem& branch);

QString defaultNodeAcrossName(int id);

QString defaultPassiveThroughName(int id);

QString defaultActiveThroughName(int id);

bool isAutoThroughName(const QString& name);

int branchNumericId(const BranchItem& branch);

QString branchFlowSymbol(const BranchItem& branch);

QString branchAcrossSymbol(const BranchItem& branch);

QString branchStorageAcrossSymbol(const BranchItem& branch);

QString branchSourceInputSymbol(const BranchItem& branch);

QString branchSourceInputDisplay(const BranchItem& branch);

void applySourceThroughNaming(BranchItem* branch);

QString defaultTwoPortModulus(TwoPortKind kind);

QString twoPortElementalEquationText(TwoPortKind kind, const QString& modulus);

}  // namespace lg
