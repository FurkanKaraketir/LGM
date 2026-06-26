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

bool isInertiaConstant(const BranchItem& branch) {
    const QString constant = branch.elementConstant().trimmed();
    if (constant.isEmpty()) {
        return false;
    }
    const QChar c = constant.at(0).toUpper();
    return c == QLatin1Char('I') || c == QLatin1Char('J');
}

bool TreeScore::betterThan(const TreeScore& other) const {
    if (sameSpanTreeConflicts != other.sameSpanTreeConflicts) {
        return sameSpanTreeConflicts < other.sameSpanTreeConflicts;
    }
    if (aPassive != other.aPassive) {
        return aPassive > other.aPassive;
    }
    if (dPassive != other.dPassive) {
        return dPassive > other.dPassive;
    }
    if (portInTree != other.portInTree) {
        return portInTree > other.portInTree;
    }
    if (externalOnPortSpanInTree != other.externalOnPortSpanInTree) {
        return externalOnPortSpanInTree < other.externalOnPortSpanInTree;
    }
    return tPassive < other.tPassive;
}

UnionFind::UnionFind(int n) : m_parent(n), m_rank(n, 0), m_components(n) {
    std::iota(m_parent.begin(), m_parent.end(), 0);
}

int UnionFind::find(int x) {
    while (m_parent[x] != x) {
        m_parent[x] = m_parent[m_parent[x]];
        x = m_parent[x];
    }
    return x;
}

bool UnionFind::connected(int a, int b) {
    return find(a) == find(b);
}

bool UnionFind::unite(int a, int b) {
    a = find(a);
    b = find(b);
    if (a == b) {
        return false;
    }
    if (m_rank[a] < m_rank[b]) {
        std::swap(a, b);
    }
    m_parent[b] = a;
    if (m_rank[a] == m_rank[b]) {
        ++m_rank[a];
    }
    --m_components;
    return true;
}

int UnionFind::components() const {
    return m_components;
}

int connectedComponents(int n, const std::vector<Edge>& edges) {
    UnionFind uf(n);
    for (const Edge& edge : edges) {
        uf.unite(edge.u, edge.v);
    }
    return uf.components();
}

TreeScore scoreTree(const std::vector<BranchItem*>& tree,
                    const std::vector<BranchItem*>& branches,
                    const std::vector<TwoPortItem*>& twoPorts) {
    TreeScore score;
    const std::unordered_set<BranchItem*> inTree(tree.begin(), tree.end());
    for (BranchItem* branch : tree) {
        if (!branch || branch->isActive()) {
            continue;
        }
        if (isTwoPortInternalBranch(*branch)) {
            ++score.portInTree;
        }
        if (isExternalBranchOnPortSpan(*branch, branches)) {
            ++score.externalOnPortSpanInTree;
        }
        switch (effectivePassiveBranchType(*branch)) {
        case BranchType::A:
            ++score.aPassive;
            break;
        case BranchType::D:
            ++score.dPassive;
            break;
        case BranchType::T:
            ++score.tPassive;
            break;
        }
    }
    std::unordered_set<const TwoPortItem*> scoredTwoPorts;
    for (TwoPortItem* item : twoPorts) {
        if (!item || !scoredTwoPorts.insert(item).second) {
            continue;
        }
        BranchItem* left = item->leftBranch();
        BranchItem* right = item->rightBranch();
        if (left && right && inTree.count(left) != 0 && inTree.count(right) != 0) {
            ++score.sameSpanTreeConflicts;
        }
        for (BranchItem* port : {left, right}) {
            if (!port || inTree.count(port) == 0) {
                continue;
            }
            if (BranchItem* external =
                    externalBranchParallelToPort(port, branches, twoPorts)) {
                if (inTree.count(external) != 0) {
                    ++score.sameSpanTreeConflicts;
                }
            }
        }
    }
    return score;
}

void applyTransformerChoice(TwoPortItem* item, bool rightInTree,
                            const std::vector<BranchItem*>& branches,
                            const std::vector<TwoPortItem*>& twoPorts,
                            PortConstraints& constraints) {
    BranchItem* left = item->leftBranch();
    BranchItem* right = item->rightBranch();
    if (!left || !right) {
        return;
    }
    if (rightInTree) {
        constraints.forcedIn.insert(right);
        constraints.forbidden.insert(left);
    } else {
        constraints.forcedIn.insert(left);
        constraints.forbidden.insert(right);
    }
    // Same span: port bond and parallel user branch cannot both be tree edges.
    for (BranchItem* port : {left, right}) {
        if (constraints.forcedIn.count(port) == 0) {
            continue;
        }
        if (BranchItem* external =
                externalBranchParallelToPort(port, branches, twoPorts)) {
            constraints.forbidden.insert(external);
        }
    }
}

void applyGyratorChoice(TwoPortItem* item, bool bothInTree, PortConstraints& constraints) {
    BranchItem* left = item->leftBranch();
    BranchItem* right = item->rightBranch();
    if (!left || !right) {
        return;
    }
    if (bothInTree) {
        constraints.forcedIn.insert(left);
        constraints.forcedIn.insert(right);
    } else {
        constraints.forbidden.insert(left);
        constraints.forbidden.insert(right);
    }
}

std::optional<BuildAttempt> tryBuildNormalTree(
    const std::vector<NodeItem*>& nodes, const std::vector<BranchItem*>& branches,
    const PortConstraints& constraints,
    const std::unordered_set<BranchItem*>& ignoredBranches) {
    const int n = static_cast<int>(nodes.size());
    if (n <= 1) {
        return BuildAttempt{};
    }

    std::unordered_map<NodeItem*, int> nodeIndex;
    nodeIndex.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        nodeIndex[nodes[static_cast<size_t>(i)]] = i;
    }

    auto makeEdge = [&](BranchItem* branch) -> std::optional<Edge> {
        if (!branch || constraints.forbidden.count(branch) != 0 ||
            ignoredBranches.count(branch) != 0) {
            return std::nullopt;
        }
        if (!branch->from() || !branch->to()) {
            return std::nullopt;
        }
        const auto fromIt = nodeIndex.find(branch->from());
        const auto toIt = nodeIndex.find(branch->to());
        if (fromIt == nodeIndex.end() || toIt == nodeIndex.end()) {
            return std::nullopt;
        }
        return Edge{branch, fromIt->second, toIt->second};
    };

    std::vector<Edge> usableEdges;
    usableEdges.reserve(branches.size());
    for (BranchItem* branch : branches) {
        const std::optional<Edge> edge = makeEdge(branch);
        if (edge) {
            usableEdges.push_back(*edge);
        }
    }

    const int componentCount = connectedComponents(n, usableEdges);
    const int targetSize = n - componentCount;

    std::vector<Edge> forcedIn;
    std::vector<Edge> optionalA;
    std::vector<Edge> optionalAOnPortSpan;
    std::vector<Edge> optionalD;
    std::vector<Edge> optionalT;
    std::vector<Edge> optionalTOnPortSpan;
    forcedIn.reserve(branches.size());
    optionalA.reserve(branches.size());
    optionalD.reserve(branches.size());
    optionalT.reserve(branches.size());

    bool forcedCycle = false;
    for (BranchItem* branch : branches) {
        if (!branch || constraints.forbidden.count(branch) != 0 ||
            ignoredBranches.count(branch) != 0) {
            continue;
        }
        const std::optional<Edge> edge = makeEdge(branch);
        if (!edge) {
            continue;
        }
        const bool forced = constraints.forcedIn.count(branch) != 0;
        if (branch->isActive()) {
            if (branch->branchType() == BranchType::A) {
                forcedIn.push_back(*edge);
            }
            continue;
        }
        if (forced) {
            forcedIn.push_back(*edge);
            continue;
        }
        switch (effectivePassiveBranchType(*branch)) {
        case BranchType::A:
            if (isExternalBranchOnPortSpan(*branch, branches)) {
                optionalAOnPortSpan.push_back(*edge);
            } else {
                optionalA.push_back(*edge);
            }
            break;
        case BranchType::D:
            optionalD.push_back(*edge);
            break;
        case BranchType::T:
            if (isExternalBranchOnPortSpan(*branch, branches)) {
                optionalTOnPortSpan.push_back(*edge);
            } else {
                optionalT.push_back(*edge);
            }
            break;
        }
    }

    UnionFind uf(n);
    std::vector<BranchItem*> tree;
    tree.reserve(static_cast<size_t>(targetSize));

    auto addEdge = [&](const Edge& edge) -> bool {
        if (uf.connected(edge.u, edge.v)) {
            return false;
        }
        uf.unite(edge.u, edge.v);
        tree.push_back(edge.branch);
        return true;
    };

    for (const Edge& edge : forcedIn) {
        if (!addEdge(edge)) {
            if (edge.branch && edge.branch->isActive() &&
                edge.branch->branchType() == BranchType::A) {
                forcedCycle = true;
            }
        }
    }
    if (forcedCycle) {
        BuildAttempt failure;
        failure.status = NormalTreeResult::Status::ForcedCycle;
        return failure;
    }
    for (const Edge& edge : forcedIn) {
        if (!edge.branch) {
            continue;
        }
        if (std::find(tree.begin(), tree.end(), edge.branch) == tree.end()) {
            BuildAttempt failure;
            failure.status = NormalTreeResult::Status::NotConnected;
            return failure;
        }
    }

    if (static_cast<int>(tree.size()) == targetSize) {
        if (uf.components() != componentCount) {
            BuildAttempt failure;
            failure.status = NormalTreeResult::Status::NotConnected;
            return failure;
        }
        BuildAttempt success;
        success.treeBranches = tree;
        return success;
    }

    // Normal-tree step order: all A-type candidates before D, then T (minimize T in tree).
    for (const std::vector<Edge>* pool : {&optionalA, &optionalAOnPortSpan, &optionalD}) {
        for (const Edge& edge : *pool) {
            if (static_cast<int>(tree.size()) >= targetSize) {
                break;
            }
            addEdge(edge);
        }
    }

    for (const Edge& edge : optionalT) {
        if (static_cast<int>(tree.size()) >= targetSize) {
            break;
        }
        addEdge(edge);
    }
    for (const Edge& edge : optionalTOnPortSpan) {
        if (static_cast<int>(tree.size()) >= targetSize) {
            break;
        }
        addEdge(edge);
    }

    if (static_cast<int>(tree.size()) != targetSize || uf.components() != componentCount) {
        BuildAttempt failure;
        failure.status = NormalTreeResult::Status::NotConnected;
        return failure;
    }

    BuildAttempt success;
    success.treeBranches = std::move(tree);
    return success;
}

int branchWeightRank(bool active, BranchType declaredType, BranchType passiveEffective) {
    if (active) {
        return declaredType == BranchType::A ? 1 : 5;
    }
    switch (passiveEffective) {
    case BranchType::A:
        return 2;
    case BranchType::D:
        return 3;
    case BranchType::T:
        return 4;
    }
    return 5;
}

int branchWeight(const BranchItem& branch) {
    if (branch.isActive()) {
        return branchWeightRank(true, branch.branchType(), BranchType::A);
    }
    return branchWeightRank(false, branch.branchType(), effectivePassiveBranchType(branch));
}

std::optional<BuildAttempt> kruskalNormalTree(const std::vector<NodeItem*>& nodes,
                                              const std::vector<BranchItem*>& branches,
                                              const PortConstraints& constraints,
                                              const std::unordered_set<BranchItem*>& ignoredBranches) {
    const int n = static_cast<int>(nodes.size());
    if (n <= 1) {
        return BuildAttempt{};
    }

    std::unordered_map<NodeItem*, int> nodeIndex;
    nodeIndex.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        nodeIndex[nodes[static_cast<size_t>(i)]] = i;
    }

    auto makeEdge = [&](BranchItem* branch) -> std::optional<Edge> {
        if (!branch || constraints.forbidden.count(branch) != 0 ||
            ignoredBranches.count(branch) != 0) {
            return std::nullopt;
        }
        if (!branch->from() || !branch->to()) {
            return std::nullopt;
        }
        const auto fromIt = nodeIndex.find(branch->from());
        const auto toIt = nodeIndex.find(branch->to());
        if (fromIt == nodeIndex.end() || toIt == nodeIndex.end()) {
            return std::nullopt;
        }
        return Edge{branch, fromIt->second, toIt->second};
    };

    std::vector<Edge> usableEdges;
    usableEdges.reserve(branches.size());
    for (BranchItem* branch : branches) {
        const std::optional<Edge> edge = makeEdge(branch);
        if (edge) {
            usableEdges.push_back(*edge);
        }
    }

    const int componentCount = connectedComponents(n, usableEdges);
    const int targetSize = n - componentCount;

    std::vector<Edge> forcedIn;
    std::vector<Edge> optionalEdges;
    forcedIn.reserve(branches.size());
    optionalEdges.reserve(branches.size());

    bool forcedCycle = false;
    for (BranchItem* branch : branches) {
        if (!branch || constraints.forbidden.count(branch) != 0 ||
            ignoredBranches.count(branch) != 0) {
            continue;
        }
        const std::optional<Edge> edge = makeEdge(branch);
        if (!edge) {
            continue;
        }
        const bool forced = constraints.forcedIn.count(branch) != 0;
        if (branch->isActive()) {
            if (branch->branchType() == BranchType::A) {
                forcedIn.push_back(*edge);
            }
            continue;
        }
        if (forced) {
            forcedIn.push_back(*edge);
            continue;
        }
        optionalEdges.push_back(*edge);
    }

    std::sort(optionalEdges.begin(), optionalEdges.end(),
              [](const Edge& a, const Edge& b) {
                  const int wa = branchWeight(*a.branch);
                  const int wb = branchWeight(*b.branch);
                  if (wa != wb) {
                      return wa < wb;
                  }
                  return a.branch < b.branch;
              });

    UnionFind uf(n);
    std::vector<BranchItem*> tree;
    tree.reserve(static_cast<size_t>(targetSize));

    auto addEdge = [&](const Edge& edge) -> bool {
        if (uf.connected(edge.u, edge.v)) {
            return false;
        }
        uf.unite(edge.u, edge.v);
        tree.push_back(edge.branch);
        return true;
    };

    for (const Edge& edge : forcedIn) {
        if (!addEdge(edge)) {
            if (edge.branch && edge.branch->isActive() &&
                edge.branch->branchType() == BranchType::A) {
                forcedCycle = true;
            }
        }
    }
    if (forcedCycle) {
        BuildAttempt failure;
        failure.status = NormalTreeResult::Status::ForcedCycle;
        return failure;
    }
    for (const Edge& edge : forcedIn) {
        if (!edge.branch) {
            continue;
        }
        if (std::find(tree.begin(), tree.end(), edge.branch) == tree.end()) {
            BuildAttempt failure;
            failure.status = NormalTreeResult::Status::NotConnected;
            return failure;
        }
    }

    if (static_cast<int>(tree.size()) == targetSize) {
        if (uf.components() != componentCount) {
            BuildAttempt failure;
            failure.status = NormalTreeResult::Status::NotConnected;
            return failure;
        }
        BuildAttempt success;
        success.treeBranches = tree;
        return success;
    }

    for (const Edge& edge : optionalEdges) {
        if (static_cast<int>(tree.size()) >= targetSize) {
            break;
        }
        addEdge(edge);
    }

    if (static_cast<int>(tree.size()) != targetSize || uf.components() != componentCount) {
        BuildAttempt failure;
        failure.status = NormalTreeResult::Status::NotConnected;
        return failure;
    }

    BuildAttempt success;
    success.treeBranches = std::move(tree);
    return success;
}

std::unordered_map<BranchItem*, int> buildBranchIndexMap(
    const std::vector<BranchItem*>& branches) {
    std::unordered_map<BranchItem*, int> branchIndex;
    branchIndex.reserve(branches.size());
    for (int i = 0; i < static_cast<int>(branches.size()); ++i) {
        if (branches[static_cast<size_t>(i)]) {
            branchIndex[branches[static_cast<size_t>(i)]] = i;
        }
    }
    return branchIndex;
}

std::vector<int> treeBranchKey(const std::vector<BranchItem*>& tree,
                               const std::unordered_map<BranchItem*, int>& branchIndex) {
    std::vector<int> key;
    key.reserve(tree.size());
    for (BranchItem* branch : tree) {
        const auto it = branchIndex.find(branch);
        if (it != branchIndex.end()) {
            key.push_back(it->second);
        }
    }
    std::sort(key.begin(), key.end());
    return key;
}

std::vector<BranchItem*> twigsOnTreePath(const std::vector<BranchItem*>& tree, BranchItem* link) {
    if (!link || !link->from() || !link->to()) {
        return {};
    }
    NodeItem* start = link->from();
    NodeItem* goal = link->to();

    std::unordered_map<NodeItem*, std::vector<std::pair<NodeItem*, BranchItem*>>> adj;
    for (BranchItem* twig : tree) {
        if (!twig || !twig->from() || !twig->to()) {
            continue;
        }
        adj[twig->from()].emplace_back(twig->to(), twig);
        adj[twig->to()].emplace_back(twig->from(), twig);
    }

    std::unordered_map<NodeItem*, BranchItem*> parentBranch;
    std::unordered_set<NodeItem*> visited;
    std::vector<NodeItem*> queue;
    queue.push_back(start);
    visited.insert(start);

    while (!queue.empty()) {
        NodeItem* node = queue.back();
        queue.pop_back();
        if (node == goal) {
            break;
        }
        const auto adjIt = adj.find(node);
        if (adjIt == adj.end()) {
            continue;
        }
        for (const auto& [next, branch] : adjIt->second) {
            if (visited.count(next) != 0) {
                continue;
            }
            visited.insert(next);
            parentBranch[next] = branch;
            queue.push_back(next);
        }
    }

    if (visited.count(goal) == 0) {
        return {};
    }

    std::vector<BranchItem*> path;
    for (NodeItem* at = goal; at != start;) {
        const auto parentIt = parentBranch.find(at);
        if (parentIt == parentBranch.end()) {
            return {};
        }
        BranchItem* branch = parentIt->second;
        path.push_back(branch);
        at = branch->from() == at ? branch->to() : branch->from();
    }
    return path;
}


}  // namespace lg::nt
