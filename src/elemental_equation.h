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

QString defaultTwoPortModulus(TwoPortKind kind);

QString twoPortElementalEquationText(TwoPortKind kind, const QString& modulus);

}  // namespace lg
