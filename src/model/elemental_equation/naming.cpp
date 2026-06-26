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

int branchNumericId(const BranchItem& branch) {
    if (branch.serialId() > 0) {
        return branch.serialId();
    }
    static const QRegularExpression digits(QStringLiteral("(\\d+)"));
    const QRegularExpressionMatch match = digits.match(branch.name());
    if (match.hasMatch()) {
        return match.captured(1).toInt();
    }
    return branch.index() + 1;
}

int parseSourceInputIdFromName(const QString& name) {
    static const QRegularExpression pattern(
        QStringLiteral("^(?:Vs|Is|vs|Fs|Omegas|Ts|Ps|Qs|Ths|qs)(\\d+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(name.trimmed());
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

int branchSourceInputId(const BranchItem& branch) {
    if (branch.sourceInputId() > 0) {
        return branch.sourceInputId();
    }
    const int fromName = parseSourceInputIdFromName(branch.name());
    if (fromName > 0) {
        return fromName;
    }
    return 1;
}

QString branchSourceInputSymbol(const BranchItem& branch) {
    const int id = branchSourceInputId(branch);
    const SystemType systemType = branchSystemType(branch);

    if (branch.branchType() == BranchType::A) {
        switch (systemType) {
        case SystemType::Mechanical:
            return QStringLiteral("vs%1").arg(id);
        case SystemType::MechanicalRotational:
            return QStringLiteral("Omegas%1").arg(id);
        case SystemType::Electrical:
            return QStringLiteral("Vs%1").arg(id);
        case SystemType::Fluid:
            return QStringLiteral("Ps%1").arg(id);
        case SystemType::Heat:
            return QStringLiteral("Ths%1").arg(id);
        }
    }

    switch (systemType) {
    case SystemType::Mechanical:
        return QStringLiteral("Fs%1").arg(id);
    case SystemType::MechanicalRotational:
        return QStringLiteral("Ts%1").arg(id);
    case SystemType::Electrical:
        return QStringLiteral("Is%1").arg(id);
    case SystemType::Fluid:
        return QStringLiteral("Qs%1").arg(id);
    case SystemType::Heat:
        return QStringLiteral("qs%1").arg(id);
    }
    return QStringLiteral("Is%1").arg(id);
}

QString branchSourceInputDisplay(const BranchItem& branch) {
    return branchSourceInputSymbol(branch) + QStringLiteral("(t)");
}

QString inputDerivativeDisplay(const QString& inputSymbol, const QString& inputLabel) {
    if (inputLabel.endsWith(QStringLiteral("(t)"))) {
        return inputLabel.left(inputLabel.size() - 3) + QStringLiteral("_dot(t)");
    }
    return inputSymbol + QStringLiteral("_dot");
}

QString branchSourceInputDotDisplay(const BranchItem& branch) {
    return inputDerivativeDisplay(branchSourceInputSymbol(branch), branchSourceInputDisplay(branch));
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
    return defaultNodeAcrossName(id, SystemType::Mechanical);
}

QString defaultNodeAcrossName(int id, SystemType systemType) {
    switch (systemType) {
    case SystemType::Mechanical:
        return QStringLiteral("v%1").arg(id);
    case SystemType::MechanicalRotational:
        return QStringLiteral("Omega%1").arg(id);
    case SystemType::Electrical:
        return QStringLiteral("V%1").arg(id);
    case SystemType::Fluid:
        return QStringLiteral("P%1").arg(id);
    case SystemType::Heat:
        return QStringLiteral("T%1").arg(id);
    }
    return QStringLiteral("v%1").arg(id);
}

QString defaultPassiveThroughName(int id) {
    return defaultPassiveThroughName(id, SystemType::Mechanical);
}

QString defaultPassiveThroughName(int id, SystemType systemType) {
    return throughNameFromConstant(QStringLiteral("1"), id, systemType);
}

QString defaultElementConstant(int id, SystemType systemType) {
    QChar prefix;
    switch (systemType) {
    case SystemType::Mechanical:
        prefix = QLatin1Char('M');
        break;
    case SystemType::MechanicalRotational:
        prefix = QLatin1Char('J');
        break;
    case SystemType::Electrical:
    case SystemType::Fluid:
    case SystemType::Heat:
        prefix = QLatin1Char('C');
        break;
    }
    return QString(prefix) + QString::number(id);
}

namespace {

QString passiveThroughPrefix(SystemType systemType) {
    switch (systemType) {
    case SystemType::Mechanical:
        return QStringLiteral("f");
    case SystemType::MechanicalRotational:
        return QStringLiteral("T");
    case SystemType::Electrical:
        return QStringLiteral("i");
    case SystemType::Fluid:
        return QStringLiteral("Q");
    case SystemType::Heat:
        return QStringLiteral("q");
    }
    return QStringLiteral("f");
}

}  // namespace

QString throughNameFromConstant(const QString& constant, int fallbackId, SystemType systemType) {
    const QString prefix = passiveThroughPrefix(systemType);
    const QString k = constant.trimmed();
    if (!k.isEmpty() && k != QStringLiteral("1") && isValidVariableSymbol(k)) {
        const QString candidate = QStringLiteral("%1_%2").arg(prefix, k);
        if (isValidVariableSymbol(candidate)) {
            return candidate;
        }
    }
    return QStringLiteral("%1%2").arg(prefix).arg(fallbackId);
}

QString defaultActiveThroughName(int id) {
    return QStringLiteral("I%1").arg(id);
}

bool isAutoThroughName(const QString& name) {
    static const QRegularExpression pattern(
        QStringLiteral(
            "^(?:"
            "Vs\\d+|Is\\d+|"
            "vs\\d+|Fs\\d+|"
            "Omegas\\d+|Ts\\d+|"
            "Ps\\d+|Qs\\d+|"
            "Ths\\d+|qs\\d+|"
            "[fF]\\d+|[iI]\\d+|"
            "Branch \\d+"
            ")$"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(name.trimmed()).hasMatch();
}

bool isAutoPassiveThroughName(const QString& name) {
    static const QRegularExpression pattern(
        QStringLiteral("^(?:[fTiQq]_\\w+|(?:f|i|T|Q|q)\\d+|Branch \\d+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QString trimmed = name.trimmed();
    if (!pattern.match(trimmed).hasMatch()) {
        return false;
    }
    if (trimmed.startsWith(QStringLiteral("Branch "), Qt::CaseInsensitive)) {
        return true;
    }
    if (trimmed.contains(QLatin1Char('_'))) {
        return isValidVariableSymbol(trimmed);
    }
    if (trimmed.startsWith(QStringLiteral("f"), Qt::CaseInsensitive) ||
        trimmed.startsWith(QStringLiteral("i"), Qt::CaseInsensitive)) {
        return trimmed.size() > 1 && trimmed.at(1).isDigit();
    }
    return trimmed.size() > 1 && trimmed.at(trimmed.size() - 1).isDigit();
}

bool isAutoPassiveThroughName(const BranchItem& branch) {
    if (branch.isActive()) {
        return false;
    }
    return isAutoPassiveThroughName(branch.name());
}

namespace {

bool isAutoAcrossName(const QString& name) {
    static const QRegularExpression pattern(QStringLiteral("^[vV]\\d+$"));
    return pattern.match(name.trimmed()).hasMatch();
}

QString autoAcrossName(int id, SystemType systemType) {
    return defaultNodeAcrossName(id, systemType);
}

QString autoPassiveThroughName(int id, SystemType systemType) {
    return defaultPassiveThroughName(id, systemType);
}

int autoSymbolNumericId(const QString& name) {
    static const QRegularExpression digits(QStringLiteral("(\\d+)"));
    const QRegularExpressionMatch match = digits.match(name.trimmed());
    return match.hasMatch() ? match.captured(1).toInt() : 1;
}

QString convertAutoAcrossName(const QString& name, SystemType systemType) {
    if (!isAutoAcrossName(name)) {
        return name;
    }
    return autoAcrossName(autoSymbolNumericId(name), systemType);
}

QString convertAutoPassiveThroughName(const QString& name, SystemType systemType) {
    if (!isAutoPassiveThroughName(name)) {
        return name;
    }
    static const QRegularExpression withConstant(
        QStringLiteral("^(?:f|i|T|Q|q)_(.+)$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch constantMatch = withConstant.match(name.trimmed());
    if (constantMatch.hasMatch()) {
        return throughNameFromConstant(constantMatch.captured(1), 1, systemType);
    }
    return throughNameFromConstant(QStringLiteral("1"), autoSymbolNumericId(name), systemType);
}

}  // namespace

const NodeItem* domainNodeForBranch(const BranchItem& branch) {
    const NodeItem* from = branch.from();
    const NodeItem* to = branch.to();
    if (from && !from->isGround()) {
        return from;
    }
    if (to && !to->isGround()) {
        return to;
    }
    return nullptr;
}

void applySourceThroughNaming(BranchItem* branch) {
    if (!branch || !branch->isActive()) {
        return;
    }
    if (branch->branchType() != BranchType::T && branch->branchType() != BranchType::A) {
        return;
    }
    if (!isAutoThroughName(branch->name()) && !isAutoPassiveThroughName(*branch)) {
        return;
    }
    branch->setName(branchSourceInputSymbol(*branch));
}

void applyConstantThroughNaming(BranchItem* branch) {
    if (!branch || branch->isActive() || isTwoPortInternalBranch(*branch)) {
        return;
    }
    if (!branch->name().trimmed().isEmpty() && !isAutoPassiveThroughName(*branch)) {
        return;
    }
    branch->setName(
        throughNameFromConstant(branch->elementConstant(), branchNumericId(*branch),
                                branchSystemType(*branch)));
}

void applyNodeDomainNaming(NodeItem* node) {
    if (!node || node->isGround()) {
        return;
    }
    const SystemType type = node->systemType();
    if (isAutoAcrossName(node->acrossVariable())) {
        node->setAcrossVariable(convertAutoAcrossName(node->acrossVariable(), type));
    }
    for (BranchItem* branch : node->branches()) {
        if (domainNodeForBranch(*branch) != node) {
            continue;
        }
        if (branch->isActive() && branch->branchType() == BranchType::T) {
            applySourceThroughNaming(branch);
        } else if (isAutoPassiveThroughName(*branch)) {
            branch->setName(convertAutoPassiveThroughName(branch->name(), type));
        }
        branch->update();
    }
}

}  // namespace lg
