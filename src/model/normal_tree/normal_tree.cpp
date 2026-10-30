#include "normal_tree.h"
#include "normal_tree_detail.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <algorithm>
#include <set>
#include <vector>

namespace lg {

using namespace nt;

void populateNormalTreeStateVariables(NormalTreeResult& result,
                                      const std::vector<BranchItem*>& branches,
                                      const std::vector<TwoPortItem*>& twoPorts) {
    extractStateVariables(result, branches, twoPorts);
}

NormalTreeEnumerationResult enumerateNormalTrees(const std::vector<NodeItem*>& nodes,
                                                 const std::vector<BranchItem*>& branches,
                                                 const std::vector<TwoPortItem*>& twoPorts) {
    NormalTreeEnumerationResult result;
    const int n = static_cast<int>(nodes.size());
    if (n == 0) {
        result.status = NormalTreeResult::Status::Incomplete;
        result.message = QStringLiteral("The graph has no nodes.");
        return result;
    }

    std::vector<TwoPortItem*> orderedTwoPorts;
    orderedTwoPorts.reserve(twoPorts.size());
    for (TwoPortItem* item : twoPorts) {
        if (item) {
            orderedTwoPorts.push_back(item);
        }
    }

    if (n == 1) {
        NormalTreeResult single;
        single.status = NormalTreeResult::Status::Ok;
        extractStateVariables(single, branches, orderedTwoPorts);
        result.status = NormalTreeResult::Status::Ok;
        result.trees.push_back(std::move(single));
        return result;
    }

    std::set<std::vector<int>> seenKeys;
    std::vector<std::vector<BranchItem*>> collected;
    bool capped = false;

    if (orderedTwoPorts.empty()) {
        enumerateTreesForPortAssignment(nodes, branches, orderedTwoPorts, PortConstraints{},
                                        seenKeys, collected, capped);
    } else {
        collectTreesForPortAssignments(nodes, branches, orderedTwoPorts, 0, PortConstraints{},
                                       seenKeys, collected, capped, {});
    }

    if (collected.empty()) {
        const std::optional<BuildAttempt> probe = kruskalNormalTree(nodes, branches, PortConstraints{});
        if (probe && probe->status == NormalTreeResult::Status::ForcedCycle) {
            result.status = NormalTreeResult::Status::ForcedCycle;
            result.message = QStringLiteral("A-type active elements form a loop; "
                                            "compatibility is violated.");
            return result;
        }
        result.status = orderedTwoPorts.empty() ? NormalTreeResult::Status::Incomplete
                                                : NormalTreeResult::Status::TwoPortConstraint;
        result.message =
            orderedTwoPorts.empty()
                ? QStringLiteral("Could not form a spanning normal tree with the "
                                   "current elements and constraints.")
                : QStringLiteral("Could not form a normal tree satisfying transformer "
                                   "(exactly one branch) and gyrator (zero or both "
                                   "branches) rules.");
        return result;
    }

    // Equal scores fall back to the twigs' serial ids so the first tree never depends on memory layout.
    const auto serialKey = [](const std::vector<BranchItem*>& tree) {
        std::vector<int> key;
        for (const BranchItem* branch : tree) {
            key.push_back(branch->serialId());
        }
        std::sort(key.begin(), key.end());
        return key;
    };
    std::sort(collected.begin(), collected.end(),
              [&](const std::vector<BranchItem*>& left, const std::vector<BranchItem*>& right) {
                  const TreeScore leftScore = scoreTree(left, branches, orderedTwoPorts);
                  const TreeScore rightScore = scoreTree(right, branches, orderedTwoPorts);
                  if (leftScore.betterThan(rightScore) || rightScore.betterThan(leftScore)) {
                      return leftScore.betterThan(rightScore);
                  }
                  return serialKey(left) < serialKey(right);
              });
    // Only the best-ranked trees are offered; alternatives must tie the first one.
    const TreeScore bestScore = scoreTree(collected.front(), branches, orderedTwoPorts);
    collected.erase(std::remove_if(collected.begin(), collected.end(),
                                   [&](const std::vector<BranchItem*>& tree) {
                                       return bestScore.betterThan(
                                           scoreTree(tree, branches, orderedTwoPorts));
                                   }),
                    collected.end());

    result.trees.reserve(collected.size());
    for (const std::vector<BranchItem*>& tree : collected) {
        NormalTreeResult entry;
        entry.status = NormalTreeResult::Status::Ok;
        entry.treeBranches = tree;
        extractStateVariables(entry, branches, orderedTwoPorts);
        result.trees.push_back(std::move(entry));
    }

    result.status = NormalTreeResult::Status::Ok;
    if (capped) {
        result.message =
            QStringLiteral("Enumeration stopped after %1 trees; more may exist.")
                .arg(kMaxEnumeratedNormalTrees);
    }
    return result;
}

NormalTreeResult computeNormalTree(const std::vector<NodeItem*>& nodes,
                                   const std::vector<BranchItem*>& branches,
                                   const std::vector<TwoPortItem*>& twoPorts) {
    const NormalTreeEnumerationResult all = enumerateNormalTrees(nodes, branches, twoPorts);
    NormalTreeResult result;
    if (all.trees.empty()) {
        result.status = all.status;
        result.message = all.message;
        return result;
    }
    return all.trees.front();
}

NormalTreeResult validateManualNormalTree(const std::vector<NodeItem*>& nodes,
                                          const std::vector<BranchItem*>& branches,
                                          const std::vector<TwoPortItem*>& twoPorts,
                                          const std::vector<BranchItem*>& treeBranches) {
    NormalTreeResult result;
    const int n = static_cast<int>(nodes.size());
    if (n == 0) {
        result.status = NormalTreeResult::Status::Incomplete;
        result.message = QStringLiteral("The graph has no nodes.");
        return result;
    }

    std::vector<TwoPortItem*> orderedTwoPorts;
    orderedTwoPorts.reserve(twoPorts.size());
    for (TwoPortItem* item : twoPorts) {
        if (item) {
            orderedTwoPorts.push_back(item);
        }
    }

    std::unordered_set<BranchItem*> inTree;
    inTree.reserve(treeBranches.size());
    for (BranchItem* branch : treeBranches) {
        if (!branch) {
            continue;
        }
        if (!branch->from() || !branch->to()) {
            result.status = NormalTreeResult::Status::Incomplete;
            result.message = QStringLiteral("A tree branch is not fully connected.");
            return result;
        }
        inTree.insert(branch);
    }

    for (BranchItem* branch : branches) {
        if (!branch) {
            continue;
        }
        if (branch->isActive() && branch->branchType() == BranchType::A && inTree.count(branch) == 0) {
            result.status = NormalTreeResult::Status::ForcedCycle;
            result.message = QStringLiteral("Active A-type sources must be in the normal tree.");
            return result;
        }
        if (branch->isActive() && branch->branchType() == BranchType::T && inTree.count(branch) != 0) {
            result.status = NormalTreeResult::Status::NotConnected;
            result.message = QStringLiteral("Active T-type sources must not be in the normal tree.");
            return result;
        }
    }

    for (TwoPortItem* item : orderedTwoPorts) {
        BranchItem* left = item->leftBranch();
        BranchItem* right = item->rightBranch();
        const bool leftIn = left && inTree.count(left) != 0;
        const bool rightIn = right && inTree.count(right) != 0;
        if (item->kind() == TwoPortKind::Transformer) {
            if (leftIn == rightIn) {
                result.status = NormalTreeResult::Status::TwoPortConstraint;
                result.message = QStringLiteral(
                    "Each transformer must have exactly one port branch in the normal tree.");
                return result;
            }
        } else if (leftIn != rightIn) {
            result.status = NormalTreeResult::Status::TwoPortConstraint;
            result.message =
                QStringLiteral("Each gyrator must have zero or both port branches in the tree.");
            return result;
        }
    }

    if (scoreTree(treeBranches, branches, orderedTwoPorts).sameSpanTreeConflicts != 0) {
        result.status = NormalTreeResult::Status::TwoPortConstraint;
        result.message = QStringLiteral(
            "A two-port port branch and a parallel element cannot both be in the tree.");
        return result;
    }

    std::unordered_map<NodeItem*, int> nodeIndex;
    nodeIndex.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        nodeIndex[nodes[static_cast<size_t>(i)]] = i;
    }

    auto makeEdge = [&](BranchItem* branch) -> std::optional<Edge> {
        if (!branch || !branch->from() || !branch->to()) {
            return std::nullopt;
        }
        const auto fromIt = nodeIndex.find(branch->from());
        const auto toIt = nodeIndex.find(branch->to());
        if (fromIt == nodeIndex.end() || toIt == nodeIndex.end()) {
            return std::nullopt;
        }
        return Edge{branch, fromIt->second, toIt->second};
    };

    std::vector<Edge> allEdges;
    allEdges.reserve(branches.size());
    for (BranchItem* branch : branches) {
        const std::optional<Edge> edge = makeEdge(branch);
        if (edge) {
            allEdges.push_back(*edge);
        }
    }

    const int componentCount = connectedComponents(n, allEdges);
    const int targetSize = n - componentCount;

    UnionFind uf(n);
    std::vector<BranchItem*> tree;
    tree.reserve(static_cast<size_t>(targetSize));
    bool forcedCycle = false;
    for (BranchItem* branch : treeBranches) {
        if (!branch) {
            continue;
        }
        const std::optional<Edge> edge = makeEdge(branch);
        if (!edge) {
            continue;
        }
        if (uf.connected(edge->u, edge->v)) {
            if (branch->isActive() && branch->branchType() == BranchType::A) {
                forcedCycle = true;
            }
            result.status = forcedCycle ? NormalTreeResult::Status::ForcedCycle
                                        : NormalTreeResult::Status::NotConnected;
            result.message = forcedCycle
                                 ? QStringLiteral("Active A-type sources cannot form a cycle.")
                                 : QStringLiteral("The selected tree branches contain a cycle.");
            return result;
        }
        uf.unite(edge->u, edge->v);
        tree.push_back(branch);
    }

    if (static_cast<int>(tree.size()) != targetSize) {
        result.status = NormalTreeResult::Status::NotConnected;
        result.message = QStringLiteral("The selection is not a spanning normal tree (%1 of %2 "
                                        "branches).")
                             .arg(tree.size())
                             .arg(targetSize);
        return result;
    }
    if (uf.components() != componentCount) {
        result.status = NormalTreeResult::Status::NotConnected;
        result.message = QStringLiteral("The tree does not connect all graph components.");
        return result;
    }

    // Normal = most A-type and fewest T-type twigs for this two-port assignment; the greedy tree
    // is optimal for both counts at once (matroid), so compare against it.
    PortConstraints assignment;
    for (TwoPortItem* item : orderedTwoPorts) {
        for (BranchItem* port : {item->leftBranch(), item->rightBranch()}) {
            if (port) {
                (inTree.count(port) != 0 ? assignment.forcedIn : assignment.forbidden).insert(port);
            }
        }
    }
    const auto countPassive = [](const std::vector<BranchItem*>& twigs, BranchType type) {
        return std::count_if(twigs.begin(), twigs.end(), [&](const BranchItem* twig) {
            return twig && !twig->isActive() && !isTwoPortInternalBranch(*twig) &&
                   effectivePassiveBranchType(*twig) == type;
        });
    };
    const std::optional<BuildAttempt> best = kruskalNormalTree(nodes, branches, assignment);
    if (best && best->status == NormalTreeResult::Status::Ok &&
        (countPassive(tree, BranchType::A) != countPassive(best->treeBranches, BranchType::A) ||
         countPassive(tree, BranchType::T) != countPassive(best->treeBranches, BranchType::T))) {
        result.status = NormalTreeResult::Status::Incomplete;
        result.message = QStringLiteral("The selection is a spanning tree but not a normal tree: it "
                                        "needs %1 A-type and %2 T-type passive twigs.")
                             .arg(countPassive(best->treeBranches, BranchType::A))
                             .arg(countPassive(best->treeBranches, BranchType::T));
        return result;
    }

    result.status = NormalTreeResult::Status::Ok;
    result.treeBranches = std::move(tree);
    extractStateVariables(result, branches, orderedTwoPorts);
    return result;
}



}  // namespace lg
