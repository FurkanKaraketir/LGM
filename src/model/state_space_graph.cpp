#include "state_space.h"
#include "state_space_detail.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/subs.h>
#include <symengine/mul.h>
#include <symengine/subs.h>

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace lg::ss {

using SymEngine::add;
using SymEngine::eq;
using SymEngine::expand;
using SymEngine::integer;
using SymEngine::div;
using SymEngine::mul;
using SymEngine::neg;
using SymEngine::sub;
using SymEngine::subs;

QString storageStateSymbol(const BranchItem& branch, bool inTree) {
    const BranchType type = effectivePassiveBranchType(branch);
    if (type == BranchType::A && inTree) {
        return branchAcrossSymbol(branch);
    }
    if (!branch.isActive()) {
        const QString k = branch.elementConstant().trimmed();
        const QChar c = k.isEmpty() ? QChar() : k.at(0).toUpper();
        if (c == QLatin1Char('I') || c == QLatin1Char('J')) {
            const QString across = branchAcrossSymbol(branch);
            if (!across.isEmpty() && across != branchThroughSymbol(branch)) {
                return across;
            }
        }
        // ponytail: K maps to effective T but co-tree storage is still the through (f_K)
        if (c == QLatin1Char('K') && type == BranchType::T) {
            return branchThroughSymbol(branch);
        }
    }
    return branchThroughSymbol(branch);
}

bool isStateBranch(const NormalTreeResult& tree, BranchItem* branch,
                   const std::vector<BranchItem*>& branches,
                   const std::vector<TwoPortItem*>& twoPorts) {
    if (!branch || branch->isActive() || isTwoPortInternalBranch(*branch)) {
        return false;
    }
    const bool inTree =
        std::find(tree.treeBranches.begin(), tree.treeBranches.end(), branch) !=
        tree.treeBranches.end();
    if (inTree && isExternalBranchOnPortSpan(*branch, branches) &&
        !isPortSpanStorageBranch(*branch, branches)) {
        return false;
    }
    const BranchType type = effectivePassiveBranchType(*branch);
    if (type == BranchType::A && inTree) {
        return true;
    }
    if (type == BranchType::A && !inTree &&
        isExternalBranchOnPortSpan(*branch, branches)) {
        return true;
    }
    if (type == BranchType::T && !inTree) {
        return true;
    }
    return false;
}

std::unordered_set<NodeItem*> reachableTreeNodes(NodeItem* start,
                                                  const std::vector<BranchItem*>& treeBranches,
                                                  BranchItem* excludedTwig) {
    std::unordered_set<NodeItem*> seen;
    if (!start) {
        return seen;
    }
    std::queue<NodeItem*> queue;
    seen.insert(start);
    queue.push(start);
    while (!queue.empty()) {
        NodeItem* node = queue.front();
        queue.pop();
        for (BranchItem* branch : node->branches()) {
            if (!branch || branch == excludedTwig) {
                continue;
            }
            if (std::find(treeBranches.begin(), treeBranches.end(), branch) ==
                treeBranches.end()) {
                continue;
            }
            NodeItem* other = branch->from() == node ? branch->to() : branch->from();
            if (!other || seen.count(other) != 0) {
                continue;
            }
            seen.insert(other);
            queue.push(other);
        }
    }
    return seen;
}

std::optional<std::vector<BranchItem*>> treePathBetween(NodeItem* start, NodeItem* goal,
                                                        const std::vector<BranchItem*>& treeBranches) {
    if (!start || !goal) {
        return std::nullopt;
    }
    if (start == goal) {
        return std::vector<BranchItem*>{};
    }
    std::unordered_map<NodeItem*, BranchItem*> parentEdge;
    std::queue<NodeItem*> queue;
    parentEdge[start] = nullptr;
    queue.push(start);
    while (!queue.empty()) {
        NodeItem* node = queue.front();
        queue.pop();
        for (BranchItem* branch : node->branches()) {
            if (!branch) {
                continue;
            }
            if (std::find(treeBranches.begin(), treeBranches.end(), branch) == treeBranches.end()) {
                continue;
            }
            NodeItem* other = branch->from() == node ? branch->to() : branch->from();
            if (!other || parentEdge.count(other) != 0) {
                continue;
            }
            parentEdge[other] = branch;
            if (other == goal) {
                std::vector<BranchItem*> path;
                for (NodeItem* at = goal; at != start;) {
                    BranchItem* edge = parentEdge[at];
                    if (!edge) {
                        return std::nullopt;
                    }
                    path.push_back(edge);
                    at = edge->from() == at ? edge->to() : edge->from();
                }
                std::reverse(path.begin(), path.end());
                return path;
            }
            queue.push(other);
        }
    }
    return std::nullopt;
}

RCP<const Basic> signedAcross(BranchItem* branch, NodeItem* from, NodeItem* to) {
    if (!branch || !from || !to) {
        return integer(0);
    }
    const std::optional<RCP<const Basic>> across = branchAcrossExpression(*branch);
    if (!across) {
        return integer(0);
    }
    if (branch->from() == from && branch->to() == to) {
        return *across;
    }
    if (branch->from() == to && branch->to() == from) {
        return neg(*across);
    }
    return integer(0);
}

RCP<const Basic> signedThrough(BranchItem* branch, NodeItem* from, NodeItem* to) {
    if (!branch || !from || !to) {
        return integer(0);
    }
    const RCP<const Basic> flow = symOf(branchThroughSymbol(*branch));
    if (branch->from() == from && branch->to() == to) {
        return flow;
    }
    if (branch->from() == to && branch->to() == from) {
        return neg(flow);
    }
    return integer(0);
}

RCP<const Basic> signedThroughForContinuityCut(BranchItem* branch, NodeItem* from, NodeItem* to,
                                               BranchItem* /*twig*/,
                                               const std::vector<BranchItem*>& /*branches*/,
                                               const std::vector<TwoPortItem*>& /*twoPorts*/) {
    if (!branch) {
        return integer(0);
    }
    return signedThrough(branch, from, to);
}

namespace {

const NodeItem* otherAcrossNode(const TwoPortItem& twoPort, const NodeItem* across) {
    if (twoPort.v1() == across) {
        return twoPort.v2();
    }
    if (twoPort.v2() == across) {
        return twoPort.v1();
    }
    return nullptr;
}

}  // namespace

std::optional<RCP<const Basic>> transformerModulusProductBetween(
    const NodeItem* fromAcross, const NodeItem* toAcross,
    const std::vector<TwoPortItem*>& twoPorts) {
    if (!fromAcross || !toAcross || fromAcross == toAcross) {
        return integer(1);
    }
    std::vector<const TwoPortItem*> path;
    std::unordered_set<const NodeItem*> seen;
    std::queue<std::pair<const NodeItem*, std::vector<const TwoPortItem*>>> queue;
    seen.insert(fromAcross);
    queue.push({fromAcross, {}});
    while (!queue.empty()) {
        const auto [node, soFar] = queue.front();
        queue.pop();
        if (node == toAcross) {
            path = soFar;
            break;
        }
        for (TwoPortItem* twoPort : twoPorts) {
            if (!twoPort || twoPort->kind() != TwoPortKind::Transformer) {
                continue;
            }
            const NodeItem* onPort = nullptr;
            if (twoPort->v1() == node) {
                onPort = twoPort->v1();
            } else if (twoPort->v2() == node) {
                onPort = twoPort->v2();
            }
            if (!onPort) {
                continue;
            }
            const NodeItem* next = otherAcrossNode(*twoPort, onPort);
            if (!next || seen.count(next) != 0) {
                continue;
            }
            std::vector<const TwoPortItem*> extended = soFar;
            extended.push_back(twoPort);
            seen.insert(next);
            queue.push({next, std::move(extended)});
        }
    }
    if (path.empty()) {
        return std::nullopt;
    }
    RCP<const Basic> product = integer(1);
    for (const TwoPortItem* twoPort : path) {
        product = mul(product, lg::twoPortModulusExpr(*twoPort));
    }
    return product;
}

std::optional<RCP<const Basic>> composeReflectedCoTreeForce(
    const QString& startPortFlow, const QString& complianceFlow,
    const std::unordered_map<QString, RCP<const Basic>>& replacements,
    const std::vector<QString>& /*branchThroughSymbols*/) {
    if (startPortFlow.isEmpty() || complianceFlow.isEmpty()) {
        return std::nullopt;
    }
    const std::unordered_map<QString, RCP<const Basic>> resolved = resolveReplacements(replacements);
    const SymEngine::map_basic_basic subMap = substitutionMap(resolved);
    const RCP<const Basic> complianceSym = symOf(complianceFlow);
    RCP<const Basic> expr = expand(subs(symOf(startPortFlow), subMap));
    if (eq(*linearCoeffRCP(expr, complianceSym), *integer(0))) {
        return std::nullopt;
    }
    for (const auto& [name, replacement] : resolved) {
        if (!isValidVariableSymbol(name) || name == complianceFlow) {
            continue;
        }
        if (!eq(*linearCoeffRCP(expr, symOf(name)), *integer(0))) {
            return std::nullopt;
        }
        (void)replacement;
    }
    return expr;
}

std::vector<QString> collectNodeAcrossSymbols(const std::vector<NodeItem*>& nodes) {
    std::vector<QString> symbols;
    for (NodeItem* node : nodes) {
        if (!node || node->isGround()) {
            continue;
        }
        const QString across = node->acrossVariable().trimmed();
        if (across.isEmpty() || across == QStringLiteral("0")) {
            continue;
        }
        if (isValidVariableSymbol(across)) {
            symbols.push_back(across);
        }
    }
    return symbols;
}

}  // namespace lg::ss

namespace lg {

std::vector<GraphOutputVariable> collectOutputVariableChoices(
    const std::vector<NodeItem*>& nodes, const std::vector<BranchItem*>& branches) {
    std::vector<GraphOutputVariable> out;
    std::unordered_set<QString> seen;

    const auto add = [&](const QString& symbol, const QString& label) {
        if (symbol.isEmpty() || symbol == QStringLiteral("0") || !isValidVariableSymbol(symbol)) {
            return;
        }
        if (seen.count(symbol) != 0) {
            return;
        }
        seen.insert(symbol);
        out.push_back({symbol, label});
    };

    for (BranchItem* branch : branches) {
        if (!branch) {
            continue;
        }
        const QString branchName = branch->name();
        const QString through = branchThroughSymbol(*branch);
        add(through, QStringLiteral("%1 through").arg(branchName));
        const QString across = branchAcrossSymbol(*branch);
        add(across, QStringLiteral("%1 across").arg(branchName));
    }
    for (NodeItem* node : nodes) {
        if (!node || node->isGround()) {
            continue;
        }
        const QString across = node->acrossVariable().trimmed();
        if (across.isEmpty()) {
            continue;
        }
        add(across, QStringLiteral("%1 across").arg(across));
    }

    std::sort(out.begin(), out.end(),
              [](const GraphOutputVariable& a, const GraphOutputVariable& b) {
                  return a.symbol < b.symbol;
              });
    return out;
}

}  // namespace lg
