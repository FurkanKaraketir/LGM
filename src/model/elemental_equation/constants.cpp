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

namespace {

bool isPureNumericConstant(const QString& text) {
    static const QRegularExpression pattern(
        QStringLiteral("^[+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?$"));
    return pattern.match(text.trimmed()).hasMatch();
}

}  // namespace

bool isValidElementConstant(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || isPureNumericConstant(trimmed)) {
        return false;
    }
    SymEngine::RCP<const SymEngine::Basic> expr;
    return parseElementConstant(trimmed, expr);
}

SymEngine::RCP<const SymEngine::Basic> branchElementConstantExpr(const BranchItem& branch) {
    SymEngine::RCP<const SymEngine::Basic> expr;
    if (!parseElementConstant(branch.elementConstant(), expr)) {
        return SymEngine::integer(1);
    }
    return expr;
}

namespace {

bool isMechanicalDomain(SystemType systemType) {
    return systemType == SystemType::Mechanical || systemType == SystemType::MechanicalRotational;
}

}  // namespace

bool elementalConstantInNumerator(SystemType systemType, BranchType type) {
    if (type == BranchType::A) {
        return true;
    }
    return isMechanicalDomain(systemType);
}

namespace {

QString reciprocalConstantText(const QString& k) {
    return k == QStringLiteral("1") ? QStringLiteral("1") : QStringLiteral("1/%1").arg(k);
}

QString elementalConstantDisplayText(SystemType systemType, BranchType type, const QString& k) {
    if (type == BranchType::A) {
        return k;
    }
    const bool numerator = elementalConstantInNumerator(systemType, type);
    if (type == BranchType::T) {
        return numerator ? reciprocalConstantText(k) : k;
    }
    return numerator ? k : reciprocalConstantText(k);
}

}  // namespace

SymEngine::RCP<const SymEngine::Basic> elementalConstantCoeff(SystemType systemType, BranchType type,
                                                                const SymEngine::RCP<const SymEngine::Basic>& k) {
    if (type == BranchType::A) {
        return k;
    }
    return elementalConstantInNumerator(systemType, type) ? k : SymEngine::div(SymEngine::integer(1), k);
}

std::optional<BranchType> inferPassiveBranchType(const QString& constant, SystemType systemType) {
    const QString k = constant.trimmed();
    if (k.isEmpty() || k == QStringLiteral("1")) {
        return std::nullopt;
    }
    const QChar c = k.at(0).toUpper();
    switch (systemType) {
    case SystemType::Electrical:
        if (c == QLatin1Char('R')) {
            return BranchType::D;
        }
        if (c == QLatin1Char('L')) {
            return BranchType::T;
        }
        if (c == QLatin1Char('C')) {
            return BranchType::A;
        }
        break;
    case SystemType::Mechanical:
        if (c == QLatin1Char('M')) {
            return BranchType::A;
        }
        if (c == QLatin1Char('K')) {
            return BranchType::T;
        }
        if (c == QLatin1Char('B')) {
            return BranchType::D;
        }
        break;
    case SystemType::MechanicalRotational:
        if (c == QLatin1Char('J')) {
            return BranchType::A;
        }
        if (c == QLatin1Char('K')) {
            return BranchType::T;
        }
        if (c == QLatin1Char('B')) {
            return BranchType::D;
        }
        break;
    case SystemType::Fluid:
        if (c == QLatin1Char('R')) {
            return BranchType::D;
        }
        if (c == QLatin1Char('I')) {
            return BranchType::T;
        }
        if (c == QLatin1Char('C')) {
            return BranchType::A;
        }
        break;
    case SystemType::Heat:
        if (c == QLatin1Char('C')) {
            return BranchType::A;
        }
        if (c == QLatin1Char('R')) {
            return BranchType::D;
        }
        break;
    }
    return std::nullopt;
}

BranchType effectivePassiveBranchType(const BranchItem& branch) {
    return branch.branchType();
}

QString elementalEquationText(BranchType type, SystemType systemType, bool passive, const QString& constantText) {
    if (!passive) {
        return {};
    }
    const QString k = constantText.trimmed().isEmpty() ? QStringLiteral("1") : constantText.trimmed();
    const QString coeff = elementalConstantDisplayText(systemType, type, k);

    QString through, across;
    switch (systemType) {
    case SystemType::Mechanical:
        through = QStringLiteral("f");
        across = QStringLiteral("v");
        break;
    case SystemType::MechanicalRotational:
        through = QStringLiteral("T");
        across = QString::fromUtf8("\xCE\xA9");
        break;
    case SystemType::Electrical:
        through = QStringLiteral("i");
        across = QStringLiteral("V");
        break;
    case SystemType::Fluid:
        through = QStringLiteral("Q");
        across = QStringLiteral("P");
        break;
    case SystemType::Heat:
        through = QStringLiteral("q");
        across = QStringLiteral("T");
        break;
    }

    switch (type) {
    case BranchType::A:
        return QStringLiteral("%1 = %2 * %3'").arg(through, coeff, across);
    case BranchType::T:
        return QStringLiteral("%1 = %2 * %3'").arg(across, coeff, through);
    case BranchType::D:
        return QStringLiteral("%1 = %2 * %3").arg(through, coeff, across);
    }
    return {};
}

SystemType branchSystemType(const BranchItem& branch) {
    const NodeItem* from = branch.from();
    const NodeItem* to = branch.to();
    if (from && !from->isGround()) {
        return from->systemType();
    }
    if (to && !to->isGround()) {
        return to->systemType();
    }
    return SystemType::Mechanical;
}

QString systemTypeLabel(SystemType systemType) {
    switch (systemType) {
    case SystemType::Mechanical:
        return QStringLiteral("Mechanical (Translational)");
    case SystemType::MechanicalRotational:
        return QStringLiteral("Mechanical (Rotational)");
    case SystemType::Electrical:
        return QStringLiteral("Electrical");
    case SystemType::Fluid:
        return QStringLiteral("Fluid");
    case SystemType::Heat:
        return QStringLiteral("Heat");
    }
    return QStringLiteral("Mechanical (Translational)");
}

}  // namespace lg
