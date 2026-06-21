#include "state_space_detail.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>

#include <algorithm>
#include <queue>
#include <unordered_map>

namespace lg::ss {

using SymEngine::add;
using SymEngine::integer;
using SymEngine::mul;
using SymEngine::neg;

QString storageStateSymbol(const BranchItem& branch, bool inTree) {
    const BranchType type = effectivePassiveBranchType(branch);
    if (type == BranchType::A && inTree) {
        return branchAcrossSymbol(branch);
    }
    return branchFlowSymbol(branch);
}

bool isStateBranch(const NormalTreeResult& tree, BranchItem* branch) {
    if (!branch || branch->isActive() || isTwoPortInternalBranch(*branch)) {
        return false;
    }
    const bool inTree =
        std::find(tree.treeBranches.begin(), tree.treeBranches.end(), branch) !=
        tree.treeBranches.end();
    const BranchType type = effectivePassiveBranchType(*branch);
    if (type == BranchType::A && inTree) {
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
    const RCP<const Basic> flow = symOf(branchFlowSymbol(*branch));
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
