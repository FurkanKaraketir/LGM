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

}  // namespace lg
