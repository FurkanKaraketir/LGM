#include "normal_tree_detail.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <algorithm>
#include <cassert>
#include <deque>
#include <numeric>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lg::nt {

bool constraintsAreFeasible(const std::vector<NodeItem*>& nodes,
                            const std::vector<BranchItem*>& branches,
                            const PortConstraints& constraints,
                            const std::unordered_set<BranchItem*>& ignoredBranches) {
    const std::optional<BuildAttempt> attempt =
        tryBuildNormalTree(nodes, branches, constraints, ignoredBranches);
    return attempt && attempt->status == NormalTreeResult::Status::Ok;
}

bool acceptEnumeratedTree(const std::vector<NodeItem*>& nodes,
                          const std::vector<BranchItem*>& branches,
                          const std::vector<TwoPortItem*>& twoPorts,
                          const std::vector<BranchItem*>& tree) {
    if (scoreTree(tree, branches, twoPorts).sameSpanTreeConflicts != 0) {
        return false;
    }
    const NormalTreeResult validated =
        validateManualNormalTree(nodes, branches, twoPorts, tree);
    return validated.status == NormalTreeResult::Status::Ok;
}

void enumerateTreesForPortAssignment(
    const std::vector<NodeItem*>& nodes, const std::vector<BranchItem*>& branches,
    const std::vector<TwoPortItem*>& twoPorts, const PortConstraints& constraints,
    std::set<std::vector<int>>& seenKeys, std::vector<std::vector<BranchItem*>>& collected,
    bool& capped) {
    const std::optional<BuildAttempt> seed = kruskalNormalTree(nodes, branches, constraints);
    if (!seed || seed->status != NormalTreeResult::Status::Ok) {
        return;
    }

    const std::unordered_map<BranchItem*, int> branchIndex = buildBranchIndexMap(branches);
    std::deque<std::vector<BranchItem*>> queue;

    auto tryCollect = [&](const std::vector<BranchItem*>& tree, bool enqueue) {
        if (capped) {
            return;
        }
        const std::vector<int> key = treeBranchKey(tree, branchIndex);
        if (key.empty() || seenKeys.count(key) != 0) {
            return;
        }
        if (!acceptEnumeratedTree(nodes, branches, twoPorts, tree)) {
            return;
        }
        seenKeys.insert(key);
        collected.push_back(tree);
        if (static_cast<int>(collected.size()) >= kMaxEnumeratedNormalTrees) {
            capped = true;
            return;
        }
        if (enqueue) {
            queue.push_back(tree);
        }
    };

    tryCollect(seed->treeBranches, true);

    while (!queue.empty() && !capped) {
        const std::vector<BranchItem*> tree = queue.front();
        queue.pop_front();
        const std::unordered_set<BranchItem*> inTree(tree.begin(), tree.end());

        for (BranchItem* link : branches) {
            if (!link || inTree.count(link) != 0 || constraints.forbidden.count(link) != 0) {
                continue;
            }
            const std::vector<BranchItem*> path = twigsOnTreePath(tree, link);
            const int linkWeight = branchWeight(*link);
            for (BranchItem* twig : path) {
                if (!twig || branchWeight(*twig) != linkWeight) {
                    continue;
                }
                std::vector<BranchItem*> newTree;
                newTree.reserve(tree.size());
                for (BranchItem* branch : tree) {
                    if (branch != twig) {
                        newTree.push_back(branch);
                    }
                }
                newTree.push_back(link);
                tryCollect(newTree, true);
            }
        }
    }
}

void collectTreesForPortAssignments(
    const std::vector<NodeItem*>& nodes, const std::vector<BranchItem*>& branches,
    const std::vector<TwoPortItem*>& twoPorts, int index, PortConstraints constraints,
    std::set<std::vector<int>>& seenKeys, std::vector<std::vector<BranchItem*>>& collected,
    bool& capped, const std::unordered_set<BranchItem*>& ignoredBranches) {
    if (index >= static_cast<int>(twoPorts.size())) {
        enumerateTreesForPortAssignment(nodes, branches, twoPorts, constraints, seenKeys,
                                        collected, capped);
        return;
    }

    TwoPortItem* item = twoPorts[static_cast<size_t>(index)];
    if (!item) {
        collectTreesForPortAssignments(nodes, branches, twoPorts, index + 1, std::move(constraints),
                                       seenKeys, collected, capped, ignoredBranches);
        return;
    }

    if (item->kind() == TwoPortKind::Transformer) {
        for (const bool rightInTree : {false, true}) {
            PortConstraints next = constraints;
            applyTransformerChoice(item, rightInTree, branches, twoPorts, next);
            if (!constraintsAreFeasible(nodes, branches, next, ignoredBranches)) {
                continue;
            }
            collectTreesForPortAssignments(nodes, branches, twoPorts, index + 1, std::move(next),
                                           seenKeys, collected, capped, ignoredBranches);
        }
        return;
    }

    for (const bool bothInTree : {false, true}) {
        PortConstraints next = constraints;
        applyGyratorChoice(item, bothInTree, next);
        if (!constraintsAreFeasible(nodes, branches, next, ignoredBranches)) {
            continue;
        }
        collectTreesForPortAssignments(nodes, branches, twoPorts, index + 1, std::move(next),
                                       seenKeys, collected, capped, ignoredBranches);
    }
}

void extractStateVariables(NormalTreeResult& result, const std::vector<BranchItem*>& branches,
                           const std::vector<TwoPortItem*>& twoPorts) {
    (void)twoPorts;
    result.stateVariables.clear();
    const std::unordered_set<BranchItem*> inTree(result.treeBranches.begin(),
                                                  result.treeBranches.end());
    std::unordered_set<QString> seenSymbols;
    const auto isPassiveElement = [](const BranchItem* branch) {
        return branch && !branch->isActive() && !isTwoPortInternalBranch(*branch);
    };
    const auto appendState = [&](NormalTreeResult::StateVariable::Kind kind, QString symbol,
                                 BranchItem* branch, const QString& fallback) {
        if (symbol.isEmpty() || seenSymbols.count(symbol) != 0) {
            symbol = fallback;
        }
        seenSymbols.insert(symbol);
        result.stateVariables.push_back({kind, symbol, branch});
    };

    // Normal-tree rule: across of A-type twigs and through of T-type links. Dependent storage
    // (A-type links, T-type twigs) is already excluded by the tree, so no state is dropped here.
    for (BranchItem* branch : result.treeBranches) {
        if (isPassiveElement(branch) && effectivePassiveBranchType(*branch) == BranchType::A) {
            appendState(NormalTreeResult::StateVariable::Kind::Across, branchAcrossSymbol(*branch),
                        branch, branchThroughSymbol(*branch) + QStringLiteral("_across"));
        }
    }
    for (BranchItem* branch : branches) {
        if (isPassiveElement(branch) && inTree.count(branch) == 0 &&
            effectivePassiveBranchType(*branch) == BranchType::T) {
            appendState(NormalTreeResult::StateVariable::Kind::Through,
                        branchThroughSymbol(*branch), branch,
                        branchThroughSymbol(*branch) + QStringLiteral("_link"));
        }
    }
}

}  // namespace lg::nt
