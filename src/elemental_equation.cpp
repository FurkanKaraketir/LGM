#include "elemental_equation.h"

#include <symengine/integer.h>
#include <symengine/parser.h>
#include <symengine/symbol.h>

#include <cassert>

namespace lg {

bool isValidVariableSymbol(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    try {
        const auto expr = SymEngine::parse(trimmed.toStdString());
        return SymEngine::is_a<SymEngine::Symbol>(*expr);
    } catch (...) {
        return false;
    }
}

bool parseElementConstant(const QString& text, SymEngine::RCP<const SymEngine::Basic>& out) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    try {
        out = SymEngine::parse(trimmed.toStdString());
        return true;
    } catch (...) {
        return false;
    }
}

SymEngine::RCP<const SymEngine::Basic> branchElementConstantExpr(const BranchItem& branch) {
    SymEngine::RCP<const SymEngine::Basic> expr;
    if (!parseElementConstant(branch.elementConstant(), expr)) {
        return SymEngine::integer(1);
    }
    return expr;
}

QString elementalEquationText(BranchType type, bool passive, const QString& constantText) {
    if (!passive) {
        return {};
    }
    const QString k = constantText.trimmed().isEmpty() ? QStringLiteral("1") : constantText.trimmed();
    switch (type) {
    case BranchType::A:
        return QStringLiteral("f = %1 * e'").arg(k);
    case BranchType::T:
        return QStringLiteral("e = %1 * f'").arg(k);
    case BranchType::D:
        return QStringLiteral("f = %1 * e").arg(k);
    }
    return {};
}

QString branchAcrossVariableText(const BranchItem& branch) {
    const NodeItem* from = branch.from();
    const NodeItem* to = branch.to();
    if (!from || !to) {
        return {};
    }
    const QString vFrom = from->acrossVariable();
    const QString vTo = to->acrossVariable();
    if (from->isGround()) {
        if (to->isGround()) {
            return QStringLiteral("0");
        }
        return QStringLiteral("-%1").arg(vTo);
    }
    if (to->isGround()) {
        return vFrom;
    }
    return QStringLiteral("%1 - %2").arg(vFrom, vTo);
}

QString defaultTwoPortModulus(TwoPortKind kind) {
    return kind == TwoPortKind::Transformer ? QStringLiteral("TF") : QStringLiteral("GY");
}

QString twoPortElementalEquationText(TwoPortKind kind, const QString& modulus) {
    const QString k = modulus.trimmed().isEmpty() ? QStringLiteral("1") : modulus.trimmed();
    if (kind == TwoPortKind::Transformer) {
        return QStringLiteral("v\u2081 = %1\u00b7v\u2082; f\u2081 = \u2212f\u2082/%1").arg(k);
    }
    return QStringLiteral("v\u2081 = %1\u00b7f\u2082; f\u2081 = \u2212v\u2082/%1").arg(k);
}

namespace {

const bool kParseSelfCheck = [] {
    SymEngine::RCP<const SymEngine::Basic> expr;
    assert(parseElementConstant(QStringLiteral("1"), expr));
    assert(eq(*expr, *SymEngine::integer(1)));
    assert(parseElementConstant(QStringLiteral("C"), expr));
    assert(!parseElementConstant(QStringLiteral(""), expr));
    assert(isValidVariableSymbol(QStringLiteral("e1")));
    assert(isValidVariableSymbol(QStringLiteral("v_a")));
    assert(!isValidVariableSymbol(QStringLiteral("1")));
    assert(!isValidVariableSymbol(QStringLiteral("2*C")));
    assert(defaultTwoPortModulus(TwoPortKind::Transformer) == QStringLiteral("TF"));
    assert(defaultTwoPortModulus(TwoPortKind::Gyrator) == QStringLiteral("GY"));
    assert(twoPortElementalEquationText(TwoPortKind::Transformer, QStringLiteral("TF"))
               .contains(QStringLiteral("TF")));
    return true;
}();

}  // namespace

}  // namespace lg
