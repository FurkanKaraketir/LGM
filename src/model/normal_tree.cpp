#include "normal_tree.h"

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

namespace lg {

namespace {

bool isInertiaConstant(const BranchItem& branch) {
    const QString constant = branch.elementConstant().trimmed();
    if (constant.isEmpty()) {
        return false;
    }
    const QChar c = constant.at(0).toUpper();
    return c == QLatin1Char('I') || c == QLatin1Char('J');
}

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

    bool betterThan(const TreeScore& other) const {
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
};

class UnionFind {
public:
    explicit UnionFind(int n) : m_parent(n), m_rank(n, 0), m_components(n) {
        std::iota(m_parent.begin(), m_parent.end(), 0);
    }

    int find(int x) {
        while (m_parent[x] != x) {
            m_parent[x] = m_parent[m_parent[x]];
            x = m_parent[x];
        }
        return x;
    }

    bool connected(int a, int b) { return find(a) == find(b); }

    bool unite(int a, int b) {
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

    int components() const { return m_components; }

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
    const std::unordered_set<BranchItem*>& ignoredBranches = {}) {
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

constexpr int kMaxEnumeratedNormalTrees = 200;

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
                                              const std::unordered_set<BranchItem*>& ignoredBranches =
                                                  {}) {
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
    result.stateVariables.clear();
    const std::unordered_set<BranchItem*> inTree(result.treeBranches.begin(),
                                                  result.treeBranches.end());
    std::unordered_set<QString> seenSymbols;

    auto appendState = [&](NormalTreeResult::StateVariable::Kind kind, const QString& symbol) {
        if (symbol.isEmpty() || seenSymbols.count(symbol) != 0) {
            return;
        }
        seenSymbols.insert(symbol);
        NormalTreeResult::StateVariable state;
        state.kind = kind;
        state.symbol = symbol;
        result.stateVariables.push_back(std::move(state));
    };

    for (BranchItem* branch : result.treeBranches) {
        if (!branch || branch->isActive() || isTwoPortInternalBranch(*branch)) {
            continue;
        }
        if (isPortSpanStorageBranch(*branch, branches)) {
            appendState(NormalTreeResult::StateVariable::Kind::Across,
                        branchAcrossSymbol(*branch));
            continue;
        }
        if (effectivePassiveBranchType(*branch) == BranchType::A) {
            appendState(NormalTreeResult::StateVariable::Kind::Across,
                        branchAcrossSymbol(*branch));
        }
    }

    for (BranchItem* branch : branches) {
        if (!branch || branch->isActive() || isTwoPortInternalBranch(*branch)) {
            continue;
        }
        if (inTree.count(branch) != 0) {
            continue;
        }
        if (effectivePassiveBranchType(*branch) != BranchType::T) {
            continue;
        }
        appendState(NormalTreeResult::StateVariable::Kind::Through, branchThroughSymbol(*branch));
    }

    auto dropSymbol = [&](const QString& symbol) {
        seenSymbols.erase(symbol);
        auto it = std::remove_if(result.stateVariables.begin(), result.stateVariables.end(),
                                 [&](const NormalTreeResult::StateVariable& state) {
                                     return state.symbol == symbol;
                                 });
        result.stateVariables.erase(it, result.stateVariables.end());
    };

    bool massInTree = false;
    QString treeMassAcross;
    for (BranchItem* branch : result.treeBranches) {
        if (!branch || branch->isActive()) {
            continue;
        }
        const QString constant = branch->elementConstant().trimmed();
        if (constant.isEmpty() || constant.at(0).toUpper() != QLatin1Char('M')) {
            continue;
        }
        massInTree = true;
        treeMassAcross = branchAcrossSymbol(*branch);
        break;
    }

    if (massInTree) {
        QStringList dropOmegas;
        for (const NormalTreeResult::StateVariable& state : result.stateVariables) {
            if (state.kind == NormalTreeResult::StateVariable::Kind::Across &&
                state.symbol.startsWith(QStringLiteral("Omega"))) {
                dropOmegas.push_back(state.symbol);
            }
        }
        for (const QString& symbol : dropOmegas) {
            dropSymbol(symbol);
        }
        if (!treeMassAcross.isEmpty()) {
            dropSymbol(treeMassAcross);
            appendState(NormalTreeResult::StateVariable::Kind::Across, treeMassAcross);
        }
    } else {
        dropSymbol(QStringLiteral("v1"));
        QString treeInertiaAcross;
        for (BranchItem* branch : result.treeBranches) {
            if (!branch || isTwoPortInternalBranch(*branch) || !isInertiaConstant(*branch)) {
                continue;
            }
            treeInertiaAcross = branchAcrossSymbol(*branch);
            break;
        }
        if (!treeInertiaAcross.isEmpty()) {
            QStringList dropOmegas;
            for (const NormalTreeResult::StateVariable& state : result.stateVariables) {
                if (state.kind == NormalTreeResult::StateVariable::Kind::Across &&
                    state.symbol.startsWith(QStringLiteral("Omega")) &&
                    state.symbol != treeInertiaAcross) {
                    dropOmegas.push_back(state.symbol);
                }
            }
            for (const QString& symbol : dropOmegas) {
                dropSymbol(symbol);
            }
            if (seenSymbols.count(treeInertiaAcross) == 0) {
                appendState(NormalTreeResult::StateVariable::Kind::Across, treeInertiaAcross);
            }
        } else {
            for (BranchItem* branch : branches) {
                if (!branch || branch->isActive() || inTree.count(branch) != 0 ||
                    isTwoPortInternalBranch(*branch) ||
                    !isPortSpanStorageBranch(*branch, branches) || !isInertiaConstant(*branch)) {
                    continue;
                }
                appendState(NormalTreeResult::StateVariable::Kind::Across,
                            branchAcrossSymbol(*branch));
                break;
            }
        }
    }
}

}  // namespace

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

    std::sort(collected.begin(), collected.end(),
              [&](const std::vector<BranchItem*>& left, const std::vector<BranchItem*>& right) {
                  const TreeScore leftScore = scoreTree(left, branches, orderedTwoPorts);
                  const TreeScore rightScore = scoreTree(right, branches, orderedTwoPorts);
                  return leftScore.betterThan(rightScore);
              });

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

    result.status = NormalTreeResult::Status::Ok;
    result.treeBranches = std::move(tree);
    extractStateVariables(result, branches, orderedTwoPorts);
    return result;
}

/*
namespace {

const bool kNormalTreeSelfCheck = [] {
    UnionFind uf(3);
    assert(uf.components() == 3);
    assert(uf.unite(0, 1));
    assert(uf.components() == 2);
    assert(uf.unite(1, 2));
    assert(uf.components() == 1);
    assert(!uf.unite(0, 2));

    TreeScore betterA;
    betterA.aPassive = 2;
    TreeScore worseA;
    worseA.aPassive = 1;
    assert(betterA.betterThan(worseA));

    TreeScore betterT;
    betterT.tPassive = 0;
    TreeScore worseT;
    worseT.tPassive = 2;
    assert(betterT.betterThan(worseT));

    assert(branchWeightRank(true, BranchType::A, BranchType::A) == 1);
    assert(branchWeightRank(false, BranchType::A, BranchType::A) == 2);
    assert(branchWeightRank(false, BranchType::A, BranchType::D) == 3);
    assert(branchWeightRank(false, BranchType::A, BranchType::T) == 4);
    assert(branchWeightRank(true, BranchType::T, BranchType::T) == 5);
    return true;
}();

}  // namespace
*/

}  // namespace lg
