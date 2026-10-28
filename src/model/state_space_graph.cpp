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
