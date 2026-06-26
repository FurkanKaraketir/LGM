#pragma once

#include "normal_tree.h"
#include "canvas.h"

#include <deque>
#include <numeric>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class BranchItem;
class NodeItem;
class TwoPortItem;

namespace lg::nt {

constexpr int kMaxEnumeratedNormalTrees = 200;

struct Edge {
    BranchItem* branch = nullptr;
    int u = 0;
    int v = 0;
};

struct TreeScore {
    int aPassive = 0;
    int dPassive = 0;
    int portInTree = 0;
    int externalOnPortSpanInTree = 0;
    int sameSpanTreeConflicts = 0;
    int tPassive = 0;

    bool betterThan(const TreeScore& other) const;
};

class UnionFind {
public:
    explicit UnionFind(int n);

    int find(int x);
    bool connected(int a, int b);
    bool unite(int a, int b);
    int components() const;

private:
    std::vector<int> m_parent;
    std::vector<int> m_rank;
    int m_components;
};

struct PortConstraints {
    std::unordered_set<BranchItem*> forcedIn;
    std::unordered_set<BranchItem*> forbidden;
};

struct BuildAttempt {
    NormalTreeResult::Status status = NormalTreeResult::Status::Ok;
    std::vector<BranchItem*> treeBranches;
};

bool isInertiaConstant(const BranchItem& branch);
int connectedComponents(int n, const std::vector<Edge>& edges);
void applyTransformerChoice(TwoPortItem* item, bool rightInTree,
                            const std::vector<BranchItem*>& branches,
                            const std::vector<TwoPortItem*>& twoPorts,
                            PortConstraints& constraints);
void applyGyratorChoice(TwoPortItem* item, bool bothInTree, PortConstraints& constraints);
int branchWeightRank(bool active, BranchType declaredType, BranchType passiveEffective);
int branchWeight(const BranchItem& branch);
TreeScore scoreTree(const std::vector<BranchItem*>& tree, const std::vector<BranchItem*>& branches,
                    const std::vector<TwoPortItem*>& twoPorts);
std::unordered_map<BranchItem*, int> buildBranchIndexMap(const std::vector<BranchItem*>& branches);
std::vector<int> treeBranchKey(const std::vector<BranchItem*>& tree,
                               const std::unordered_map<BranchItem*, int>& branchIndex);
std::optional<BuildAttempt> tryBuildNormalTree(const std::vector<NodeItem*>& nodes,
                                               const std::vector<BranchItem*>& branches,
                                               const PortConstraints& constraints,
                                               const std::unordered_set<BranchItem*>& ignoredBranches = {});
std::optional<BuildAttempt> kruskalNormalTree(const std::vector<NodeItem*>& nodes,
                                              const std::vector<BranchItem*>& branches,
                                              const PortConstraints& constraints,
                                              const std::unordered_set<BranchItem*>& ignoredBranches = {});
bool constraintsAreFeasible(const std::vector<NodeItem*>& nodes,
                            const std::vector<BranchItem*>& branches,
                            const PortConstraints& constraints,
                            const std::unordered_set<BranchItem*>& ignoredBranches);
bool acceptEnumeratedTree(const std::vector<NodeItem*>& nodes,
                          const std::vector<BranchItem*>& branches,
                          const std::vector<TwoPortItem*>& twoPorts,
                          const std::vector<BranchItem*>& tree);
void enumerateTreesForPortAssignment(
    const std::vector<NodeItem*>& nodes, const std::vector<BranchItem*>& branches,
    const std::vector<TwoPortItem*>& twoPorts, const PortConstraints& constraints,
    std::set<std::vector<int>>& seenKeys, std::vector<std::vector<BranchItem*>>& collected,
    bool& capped);
void collectTreesForPortAssignments(
    const std::vector<NodeItem*>& nodes, const std::vector<BranchItem*>& branches,
    const std::vector<TwoPortItem*>& twoPorts, int index, PortConstraints constraints,
    std::set<std::vector<int>>& seenKeys, std::vector<std::vector<BranchItem*>>& collected,
    bool& capped, const std::unordered_set<BranchItem*>& ignoredBranches);
void extractStateVariables(NormalTreeResult& result, const std::vector<BranchItem*>& branches,
                           const std::vector<TwoPortItem*>& twoPorts);
std::vector<BranchItem*> twigsOnTreePath(const std::vector<BranchItem*>& tree, BranchItem* link);

}  // namespace lg::nt
