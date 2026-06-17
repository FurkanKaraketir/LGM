#include "elemental_equation.h"

#include <symengine/integer.h>
#include <symengine/parser.h>
#include <symengine/symbol.h>

#include <QRegularExpression>

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

int branchNumericId(const BranchItem& branch) {
    static const QRegularExpression digits(QStringLiteral("(\\d+)"));
    const QRegularExpressionMatch match = digits.match(branch.name());
    if (match.hasMatch()) {
        return match.captured(1).toInt();
    }
    return branch.index() + 1;
}

QString branchSourceInputSymbol(const BranchItem& branch) {
    const int id = branchNumericId(branch);
    if (branch.branchType() == BranchType::A) {
        return QStringLiteral("V%1").arg(id);
    }
    return QStringLiteral("I%1").arg(id);
}

QString branchSourceInputDisplay(const BranchItem& branch) {
    return branchSourceInputSymbol(branch) + QStringLiteral("(t)");
}

QString branchAcrossVariableText(const BranchItem& branch) {
    const NodeItem* from = branch.from();
    const NodeItem* to = branch.to();
    if (!from || !to) {
        return {};
    }
    if (branch.isActive() && branch.branchType() == BranchType::A) {
        return branchSourceInputSymbol(branch);
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

QString defaultNodeAcrossName(int id) {
    return QStringLiteral("v%1").arg(id);
}

QString defaultPassiveThroughName(int id) {
    return QStringLiteral("f%1").arg(id);
}

QString defaultActiveThroughName(int id) {
    return QStringLiteral("I%1").arg(id);
}

bool isAutoThroughName(const QString& name) {
    static const QRegularExpression pattern(
        QStringLiteral("^(?:f|I|Branch )\\d+$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(name.trimmed()).hasMatch();
}

namespace {

QString branchSymbolBase(const QString& branchName, int branchIndex) {
    const QString trimmed = branchName.trimmed();
    if (isValidVariableSymbol(trimmed)) {
        return trimmed;
    }
    const QRegularExpression digits(QStringLiteral("(\\d+)"));
    const QRegularExpressionMatch match = digits.match(trimmed);
    if (match.hasMatch()) {
        const QString candidate = QStringLiteral("f%1").arg(match.captured(1));
        if (isValidVariableSymbol(candidate)) {
            return candidate;
        }
    }
    return QStringLiteral("f%1").arg(branchIndex + 1);
}

}  // namespace

QString branchFlowSymbol(const BranchItem& branch) {
    return branchSymbolBase(branch.name(), branch.index());
}

QString branchAcrossSymbol(const BranchItem& branch) {
    if (branch.isActive() && branch.branchType() == BranchType::A) {
        return branchSourceInputSymbol(branch);
    }
    const QString text = branchAcrossVariableText(branch);
    if (isValidVariableSymbol(text)) {
        return text;
    }
    return QStringLiteral("vf%1").arg(branchNumericId(branch));
}

QString branchStorageAcrossSymbol(const BranchItem& branch) {
    return branchAcrossSymbol(branch);
}

void applySourceThroughNaming(BranchItem* branch) {
    if (!branch || !branch->isActive() || branch->branchType() != BranchType::T) {
        return;
    }
    if (!isAutoThroughName(branch->name())) {
        return;
    }
    branch->setName(defaultActiveThroughName(branchNumericId(*branch)));
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
    assert(isValidVariableSymbol(QStringLiteral("v1")));
    assert(isValidVariableSymbol(QStringLiteral("f1")));
    assert(isValidVariableSymbol(QStringLiteral("vf1")));
    assert(isValidVariableSymbol(QStringLiteral("V1")));
    assert(isValidVariableSymbol(QStringLiteral("I1")));
    assert(!isValidVariableSymbol(QStringLiteral("V1(t)")));
    assert(!isValidVariableSymbol(QStringLiteral("Branch 1")));
    assert(defaultNodeAcrossName(2) == QStringLiteral("v2"));
    assert(defaultPassiveThroughName(3) == QStringLiteral("f3"));
    assert(defaultActiveThroughName(2) == QStringLiteral("I2"));
    assert(isAutoThroughName(QStringLiteral("f2")));
    assert(isAutoThroughName(QStringLiteral("Branch 1")));
    assert(!isAutoThroughName(QStringLiteral("Fs")));
    return true;
}();

}  // namespace

}  // namespace lg
