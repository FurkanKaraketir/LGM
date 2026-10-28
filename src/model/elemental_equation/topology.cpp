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
