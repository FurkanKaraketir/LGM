#include "elemental_equation.h"

#include "canvas.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/parser.h>
#include <symengine/symbol.h>

#include <QRegularExpression>

#include <cassert>
#include <optional>

namespace lg {

QString branchSymbolBase(const BranchItem& branch) {
    const QString trimmed = branch.name().trimmed();
    const SystemType systemType = branchSystemType(branch);
    const int id = branchNumericId(branch);
    if (isValidVariableSymbol(trimmed) && !isAutoPassiveThroughName(branch) &&
        !isAutoThroughName(trimmed)) {
        return trimmed;
    }
    return throughNameFromConstant(branch.elementConstant(), id, systemType);
}

QString branchThroughSymbol(const BranchItem& branch) {
    if (branch.isActive() && branch.branchType() == BranchType::T) {
        return branchSourceInputSymbol(branch);
    }
    return branchSymbolBase(branch);
}

QString syntheticAcrossSymbolName(const BranchItem& branch) {
    const QString k = branch.elementConstant().trimmed();
    if (!k.isEmpty() && k != QStringLiteral("1") && isValidVariableSymbol(k)) {
        const QString candidate = k + QStringLiteral("_across");
        if (isValidVariableSymbol(candidate)) {
            return candidate;
        }
    }
    return branchThroughSymbol(branch) + QStringLiteral("_across");
}

QString branchAcrossSymbol(const BranchItem& branch) {
    if (branch.isActive() && branch.branchType() == BranchType::A) {
        return branchSourceInputSymbol(branch);
    }
    const QString text = branchAcrossVariableText(branch);
    if (isValidVariableSymbol(text)) {
        return text;
    }
    return syntheticAcrossSymbolName(branch);
}

bool usesSyntheticAcrossSymbol(const BranchItem& branch) {
    const QString text = branchAcrossVariableText(branch).trimmed();
    return !text.isEmpty() && text != QStringLiteral("0") && !isValidVariableSymbol(text);
}

std::optional<SymEngine::RCP<const SymEngine::Basic>> branchAcrossExpression(
    const BranchItem& branch) {
    const QString text = branchAcrossVariableText(branch).trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    if (text == QStringLiteral("0")) {
        return SymEngine::integer(0);
    }
    try {
        return SymEngine::parse(text.toStdString());
    } catch (...) {
        return std::nullopt;
    }
}

SymEngine::RCP<const SymEngine::Basic> branchNodeAcrossExpr(const BranchItem& branch) {
    if (const std::optional<SymEngine::RCP<const SymEngine::Basic>> expr = branchAcrossExpression(branch)) {
        return *expr;
    }
    return SymEngine::symbol(branchAcrossSymbol(branch).toStdString());
}

bool isTwoPortInternalBranch(const BranchItem& branch) {
    if (branch.isTwoPortPort()) {
        return true;
    }
    // ponytail: shared transformer nodes keep only one NodeItem::twoPort; flag above is authoritative.
    const NodeItem* from = branch.from();
    if (!from) {
        return false;
    }
    const TwoPortItem* twoPort = from->twoPort();
    if (!twoPort) {
        return false;
    }
    return &branch == twoPort->leftBranch() || &branch == twoPort->rightBranch();
}
SymEngine::RCP<const SymEngine::Basic> twoPortModulusExpr(const TwoPortItem& twoPort) {
    SymEngine::RCP<const SymEngine::Basic> expr;
    if (!parseElementConstant(twoPort.modulus(), expr)) {
        return SymEngine::integer(1);
    }
    return expr;
}

QString branchStorageAcrossSymbol(const BranchItem& branch) {
    return branchAcrossSymbol(branch);
}

}  // namespace lg
