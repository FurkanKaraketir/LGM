#include "elemental_equation.h"

#include <symengine/integer.h>
#include <symengine/parser.h>
#include <symengine/symbol.h>
#include <symengine/add.h>

#include <optional>

#include <symengine/mul.h>

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

}  // namespace

namespace {

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

}  // namespace

QString branchThroughSymbol(const BranchItem& branch) {
    if (branch.isActive() && branch.branchType() == BranchType::T) {
        return branchSourceInputSymbol(branch);
    }
    return branchSymbolBase(branch);
}

QString branchAcrossSymbol(const BranchItem& branch) {
    if (branch.isActive() && branch.branchType() == BranchType::A) {
        return branchSourceInputSymbol(branch);
    }
    const QString text = branchAcrossVariableText(branch);
    if (isValidVariableSymbol(text)) {
        return text;
    }
    return branchThroughSymbol(branch) + QStringLiteral("_a");
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

namespace {

bool sharesEndpointsImpl(const BranchItem* a, const BranchItem* b) {
    if (!a || !b) {
        return false;
    }
    const NodeItem* af = a->from();
    const NodeItem* at = a->to();
    const NodeItem* bf = b->from();
    const NodeItem* bt = b->to();
    return (af == bf && at == bt) || (af == bt && at == bf);
}

}  // namespace

bool sharesEndpoints(const BranchItem* a, const BranchItem* b) {
    return sharesEndpointsImpl(a, b);
}

bool isExternalBranchOnPortSpan(const BranchItem& branch,
                                const std::vector<BranchItem*>& branches) {
    if (isTwoPortInternalBranch(branch)) {
        return false;
    }
    for (BranchItem* port : branches) {
        if (!port || !isTwoPortInternalBranch(*port)) {
            continue;
        }
        if (sharesEndpoints(&branch, port)) {
            return true;
        }
    }
    return false;
}

bool isPortSpanStorageBranch(const BranchItem& branch,
                             const std::vector<BranchItem*>& branches) {
    if (!isExternalBranchOnPortSpan(branch, branches)) {
        return false;
    }
    const QString constant = branch.elementConstant().trimmed();
    if (!constant.isEmpty()) {
        const QChar c = constant.at(0).toUpper();
        if (c == QLatin1Char('M') || c == QLatin1Char('I') || c == QLatin1Char('J') ||
            c == QLatin1Char('K') || c == QLatin1Char('C') || c == QLatin1Char('L')) {
            return true;
        }
    }
    if (effectivePassiveBranchType(branch) != BranchType::A) {
        return false;
    }
    return inferPassiveBranchType(branch.elementConstant(), branchSystemType(branch)).has_value();
}

BranchItem* externalBranchParallelToPort(const BranchItem* port,
                                           const std::vector<BranchItem*>& branches,
                                           const std::vector<TwoPortItem*>& twoPorts) {
    for (BranchItem* branch : branchesParallelToPort(port, branches, twoPorts)) {
        if (!branch->isActive()) {
            return branch;
        }
    }
    return nullptr;
}

std::vector<BranchItem*> branchesParallelToPort(const BranchItem* port,
                                                const std::vector<BranchItem*>& branches,
                                                const std::vector<TwoPortItem*>& twoPorts) {
    std::vector<BranchItem*> parallels;
    if (!port || !isTwoPortInternalBranch(*port)) {
        return parallels;
    }
    for (BranchItem* branch : branches) {
        if (!branch || branch == port) {
            continue;
        }
        if (isTwoPortInternalBranch(*branch)) {
            continue;
        }
        if (sharesEndpoints(port, branch)) {
            parallels.push_back(branch);
        }
    }
    return parallels;
}

SymEngine::RCP<const SymEngine::Basic> signedParallelPortSpanFlowSum(
    const BranchItem& port, const std::vector<BranchItem*>& parallels) {
    SymEngine::RCP<const SymEngine::Basic> sum = SymEngine::integer(0);
    for (BranchItem* parallel : parallels) {
        if (!parallel) {
            continue;
        }
        sum = SymEngine::add(sum, signedParallelFlowExpr(port, *parallel));
    }
    return sum;
}

namespace {

int internalPortCountOnSpan(const BranchItem* ref, const std::vector<BranchItem*>& branches) {
    if (!ref) {
        return 0;
    }
    int count = 0;
    for (BranchItem* branch : branches) {
        if (branch && isTwoPortInternalBranch(*branch) && sharesEndpoints(ref, branch)) {
            ++count;
        }
    }
    return count;
}

bool includeInSpanJunction(const BranchItem* member, const BranchItem& port,
                           const std::vector<BranchItem*>& branches,
                           const std::vector<TwoPortItem*>& twoPorts, bool excludePort) {
    if (!member || (excludePort && member == &port)) {
        return false;
    }
    if (!sharesEndpoints(&port, member)) {
        return false;
    }
    if (isTwoPortInternalBranch(*member)) {
        if (internalPortCountOnSpan(member, branches) > 1) {
            return true;
        }
        return externalBranchParallelToPort(member, branches, twoPorts) == nullptr;
    }
    return true;
}

}  // namespace

SymEngine::RCP<const SymEngine::Basic> signedSpanJunctionFlowSum(
    const BranchItem& port, const std::vector<BranchItem*>& branches,
    const std::vector<TwoPortItem*>& twoPorts, bool excludePort) {
    SymEngine::RCP<const SymEngine::Basic> sum = SymEngine::integer(0);
    for (BranchItem* branch : branches) {
        if (!includeInSpanJunction(branch, port, branches, twoPorts, excludePort)) {
            continue;
        }
        sum = SymEngine::add(sum, signedParallelFlowExpr(port, *branch));
    }
    return sum;
}

bool omitFromContinuityCut(const BranchItem* branch,
                           const std::vector<BranchItem*>& branches,
                           const std::vector<TwoPortItem*>& twoPorts) {
    if (!branch) {
        return true;
    }
    if (!isTwoPortInternalBranch(*branch)) {
        return false;
    }
    // ponytail: stacked transformers can share one span; each port bond is an independent flow.
    if (internalPortCountOnSpan(branch, branches) > 1) {
        return false;
    }
    return externalBranchParallelToPort(branch, branches, twoPorts) != nullptr;
}

bool skipTwigFlowContinuity(const BranchItem& twig,
                            const std::vector<TwoPortItem*>& twoPorts,
                            const std::vector<BranchItem*>& branches) {
    (void)twoPorts;
    return isExternalBranchOnPortSpan(twig, branches);
}

SymEngine::RCP<const SymEngine::Basic> signedParallelFlowExpr(const BranchItem& fromRef,
                                                                const BranchItem& parallel) {
    const SymEngine::RCP<const SymEngine::Basic> flow =
        SymEngine::symbol(branchThroughSymbol(parallel).toStdString());
    if (parallel.from() == fromRef.from() && parallel.to() == fromRef.to()) {
        return flow;
    }
    if (parallel.from() == fromRef.to() && parallel.to() == fromRef.from()) {
        return SymEngine::neg(flow);
    }
    return SymEngine::integer(0);
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

QString defaultTwoPortModulus(TwoPortKind kind) {
    return kind == TwoPortKind::Transformer ? QStringLiteral("TF") : QStringLiteral("GY");
}

QString twoPortElementalEquationText(TwoPortKind kind, const QString& modulus,
                                     const BranchItem* left, const BranchItem* right) {
    const QString k = modulus.trimmed().isEmpty() ? QStringLiteral("1") : modulus.trimmed();
    if (left && right) {
        const QString leftAcross = branchAcrossVariableText(*left);
        const QString rightAcross = branchAcrossVariableText(*right);
        const QString leftThrough = branchThroughSymbol(*left);
        const QString rightThrough = branchThroughSymbol(*right);
        if (kind == TwoPortKind::Transformer) {
            return QStringLiteral("%1 = %2 * %3; %4 = -%5 / %2")
                .arg(leftAcross, k, rightAcross, leftThrough, rightThrough);
        }
        return QStringLiteral("%1 = %2 * %5; %4 = -%3 / %2")
            .arg(leftAcross, k, rightAcross, leftThrough, rightThrough);
    }
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
    assert(!isValidElementConstant(QStringLiteral("1")));
    assert(!isValidElementConstant(QStringLiteral("42")));
    assert(!isValidElementConstant(QStringLiteral("3.14")));
    assert(isValidElementConstant(QStringLiteral("R1")));
    assert(isValidElementConstant(QStringLiteral("C2")));
    assert(defaultElementConstant(3, SystemType::Mechanical) == QStringLiteral("M3"));
    assert(defaultElementConstant(2, SystemType::Electrical) == QStringLiteral("C2"));
    assert(defaultElementConstant(5, SystemType::MechanicalRotational) == QStringLiteral("J5"));
    assert(isValidVariableSymbol(QStringLiteral("v1")));
    assert(isValidVariableSymbol(QStringLiteral("f1")));
    assert(isValidVariableSymbol(QStringLiteral("f1_a")));
    assert(isValidVariableSymbol(QStringLiteral("f_m2")));
    assert(isValidVariableSymbol(QStringLiteral("i_C1")));
    assert(isValidVariableSymbol(QStringLiteral("Vs1")));
    assert(isValidVariableSymbol(QStringLiteral("Is1")));
    assert(!isValidVariableSymbol(QStringLiteral("V1(t)")));
    assert(!isValidVariableSymbol(QStringLiteral("Branch 1")));
    assert(defaultNodeAcrossName(2) == QStringLiteral("v2"));
    assert(defaultNodeAcrossName(2, SystemType::Electrical) == QStringLiteral("V2"));
    assert(defaultPassiveThroughName(3) == QStringLiteral("f3"));
    assert(defaultPassiveThroughName(3, SystemType::Electrical) == QStringLiteral("i3"));
    assert(throughNameFromConstant(QStringLiteral("m2"), 3, SystemType::Mechanical) ==
           QStringLiteral("f_m2"));
    assert(throughNameFromConstant(QStringLiteral("C1"), 2, SystemType::Electrical) ==
           QStringLiteral("i_C1"));
    assert(!isAutoPassiveThroughName(QString()));
    assert(defaultActiveThroughName(2) == QStringLiteral("I2"));
    assert(isAutoThroughName(QStringLiteral("Is3")));
    assert(isAutoThroughName(QStringLiteral("Vs2")));
    assert(parseSourceInputIdFromName(QStringLiteral("Vs2")) == 2);
    assert(parseSourceInputIdFromName(QStringLiteral("i_C2")) == 0);
    assert(inputDerivativeDisplay(QStringLiteral("Vs1"), QStringLiteral("Vs1(t)")) ==
           QStringLiteral("Vs1_dot(t)"));
    assert(isAutoThroughName(QStringLiteral("f2")));
    assert(isAutoThroughName(QStringLiteral("Branch 1")));
    assert(!isAutoThroughName(QStringLiteral("Fs")));
    assert(isAutoPassiveThroughName(QStringLiteral("f_m2")));
    assert(isAutoPassiveThroughName(QStringLiteral("i_C2")));
    assert(!isAutoPassiveThroughName(QStringLiteral("myForce")));
    assert(convertAutoAcrossName(QStringLiteral("v3"), SystemType::Electrical) == QStringLiteral("V3"));
    assert(convertAutoAcrossName(QStringLiteral("V3"), SystemType::Mechanical) == QStringLiteral("v3"));
    assert(convertAutoPassiveThroughName(QStringLiteral("f7"), SystemType::Electrical) ==
           QStringLiteral("i7"));
    assert(convertAutoPassiveThroughName(QStringLiteral("f_m2"), SystemType::Electrical) ==
           QStringLiteral("i_m2"));
    assert(convertAutoPassiveThroughName(QStringLiteral("i7"), SystemType::Mechanical) ==
           QStringLiteral("f7"));
    assert(elementalEquationText(BranchType::A, SystemType::Mechanical, true, QStringLiteral("m")) ==
           QStringLiteral("f = m * v'"));
    assert(elementalEquationText(BranchType::T, SystemType::Mechanical, true, QStringLiteral("K")) ==
           QStringLiteral("v = 1/K * f'"));
    assert(elementalEquationText(BranchType::T, SystemType::Electrical, true, QStringLiteral("L")) ==
           QStringLiteral("V = L * i'"));
    assert(elementalEquationText(BranchType::D, SystemType::Electrical, true, QStringLiteral("R")) ==
           QStringLiteral("i = 1/R * V"));
    assert(elementalEquationText(BranchType::D, SystemType::Mechanical, true, QStringLiteral("B")) ==
           QStringLiteral("f = B * v"));
    assert(elementalEquationText(BranchType::D, SystemType::MechanicalRotational, true,
                                 QStringLiteral("B")) == QStringLiteral("T = B * %1").arg(QString::fromUtf8("\xCE\xA9")));
    assert(elementalEquationText(BranchType::A, SystemType::Electrical, true, QStringLiteral("C")) ==
           QStringLiteral("i = C * V'"));
    try {
        const auto parsed = SymEngine::parse("V1 - V3");
        assert(eq(*parsed, *SymEngine::sub(SymEngine::symbol("V1"), SymEngine::symbol("V3"))));
    } catch (...) {
        assert(false);
    }
    assert(inferPassiveBranchType(QStringLiteral("R2"), SystemType::Electrical) == BranchType::D);
    assert(inferPassiveBranchType(QStringLiteral("L7"), SystemType::Electrical) == BranchType::T);
    assert(inferPassiveBranchType(QStringLiteral("C4"), SystemType::Electrical) == BranchType::A);
    assert(inferPassiveBranchType(QStringLiteral("m2"), SystemType::Mechanical) == BranchType::A);
    assert(inferPassiveBranchType(QStringLiteral("1"), SystemType::Electrical) == std::nullopt);
    assert(elementalEquationText(BranchType::D, SystemType::Fluid, true, QStringLiteral("Rp")) ==
           QStringLiteral("Q = 1/Rp * P"));
    assert(elementalConstantInNumerator(SystemType::Mechanical, BranchType::T));
    assert(elementalConstantInNumerator(SystemType::MechanicalRotational, BranchType::D));
    assert(!elementalConstantInNumerator(SystemType::Electrical, BranchType::T));
    assert(elementalEquationText(BranchType::A, SystemType::MechanicalRotational, true,
                                 QStringLiteral("J")) ==
           QStringLiteral("T = J * %1'").arg(QString::fromUtf8("\xCE\xA9")));
    assert(isValidVariableSymbol(QStringLiteral("Omega1")));
    assert(isValidVariableSymbol(QStringLiteral("F1")));
    assert(isValidVariableSymbol(QStringLiteral("T1")));
    // ponytail: shared-span omit logic; ceiling: stack-only nodes, no scene graph wiring.
    {
        NodeItem n1(5.0);
        NodeItem n2(5.0);
        BranchItem port1(&n1, &n2, 0, 3);
        BranchItem port2(&n1, &n2, 1, 3);
        BranchItem parallel(&n1, &n2, 2, 3);
        port1.setTwoPortPort(true);
        port2.setTwoPortPort(true);
        parallel.setElementConstant(QStringLiteral("I1"));
        parallel.setBranchType(BranchType::T);
        const std::vector<BranchItem*> spanBranches = {&port1, &port2, &parallel};
        const std::vector<TwoPortItem*> noTwoPorts;
        assert(omitFromContinuityCut(&port1, spanBranches, noTwoPorts));
        assert(omitFromContinuityCut(&port2, spanBranches, noTwoPorts));
        assert(!omitFromContinuityCut(&parallel, spanBranches, noTwoPorts));
        BranchItem port3(&n1, &n2, 3, 3);
        port3.setTwoPortPort(true);
        assert(!omitFromContinuityCut(&port1, {&port1, &port2, &port3, &parallel}, noTwoPorts));
        parallel.setName(QStringLiteral("f_parallel"));
        port2.setName(QStringLiteral("f_p2"));
        port3.setName(QStringLiteral("f_p3"));
        const std::vector<BranchItem*> stacked = {&port1, &port2, &port3, &parallel};
        const auto junctionSum = signedSpanJunctionFlowSum(port1, stacked, noTwoPorts, true);
        assert(eq(*junctionSum, *SymEngine::add(SymEngine::symbol("f_parallel"),
                                                 SymEngine::symbol("f_p2"),
                                                 SymEngine::symbol("f_p3"))));
    }
    {
        NodeItem v1(3.0);
        NodeItem g1(3.0);
        NodeItem v2(3.0);
        NodeItem g2(3.0);
        g1.setGround(true);
        g2.setGround(true);
        v1.setAcrossVariable(QStringLiteral("V1"));
        v2.setAcrossVariable(QStringLiteral("V2"));
        BranchItem left(&v1, &g1, 0, 1);
        BranchItem right(&v2, &g2, 1, 2);
        left.setName(QStringLiteral("i3"));
        right.setName(QStringLiteral("i4"));
        assert(twoPortElementalEquationText(TwoPortKind::Transformer, QStringLiteral("TF"), &left,
                                            &right) == QStringLiteral("V1 = TF * V2; i3 = -i4 / TF"));
        assert(twoPortElementalEquationText(TwoPortKind::Gyrator, QStringLiteral("GY"), &left,
                                            &right) == QStringLiteral("V1 = GY * i4; i3 = -V2 / GY"));
    }
    return true;
}();

}  // namespace

}  // namespace lg
