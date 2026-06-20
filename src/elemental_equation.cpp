#include "elemental_equation.h"

#include <symengine/integer.h>
#include <symengine/parser.h>
#include <symengine/symbol.h>

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
    if (branch.isActive()) {
        return branch.branchType();
    }
    if (const std::optional<BranchType> inferred =
            inferPassiveBranchType(branch.elementConstant(), branchSystemType(branch))) {
        return *inferred;
    }
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

QString branchFlowSymbol(const BranchItem& branch) {
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
    return branchFlowSymbol(branch) + QStringLiteral("_a");
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

bool isTwoPortInternalBranch(const BranchItem& branch) {
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
    if (!isAutoPassiveThroughName(*branch)) {
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
    return true;
}();

}  // namespace

}  // namespace lg
