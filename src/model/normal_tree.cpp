#include "normal_tree.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lg {

namespace {

struct Edge {
    BranchItem* branch = nullptr;
    int u = 0;
    int v = 0;
};

struct TreeScore {
    int aPassive = 0;
    int dPassive = 0;
    int tPassive = 0;

    bool betterThan(const TreeScore& other) const {
        if (aPassive != other.aPassive) {
            return aPassive > other.aPassive;
        }
        if (dPassive != other.dPassive) {
            return dPassive > other.dPassive;
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

TreeScore scoreTree(const std::vector<BranchItem*>& tree) {
    TreeScore score;
    for (BranchItem* branch : tree) {
        if (!branch || branch->isActive()) {
            continue;
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
    return score;
}

void applyTransformerChoice(TwoPortItem* item, bool rightInTree, PortConstraints& constraints) {
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
    const PortConstraints& constraints) {
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
        if (!branch || constraints.forbidden.count(branch) != 0) {
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
    std::vector<Edge> optionalD;
    std::vector<Edge> optionalT;
    forcedIn.reserve(branches.size());
    optionalA.reserve(branches.size());
    optionalD.reserve(branches.size());
    optionalT.reserve(branches.size());

    bool forcedCycle = false;
    for (BranchItem* branch : branches) {
        if (!branch || constraints.forbidden.count(branch) != 0) {
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
            optionalA.push_back(*edge);
            break;
        case BranchType::D:
            optionalD.push_back(*edge);
            break;
        case BranchType::T:
            optionalT.push_back(*edge);
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

    for (const std::vector<Edge>* pool : {&optionalA, &optionalD}) {
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

    if (static_cast<int>(tree.size()) != targetSize || uf.components() != componentCount) {
        BuildAttempt failure;
        failure.status = NormalTreeResult::Status::NotConnected;
        return failure;
    }

    BuildAttempt success;
    success.treeBranches = std::move(tree);
    return success;
}

bool constraintsAreFeasible(const std::vector<NodeItem*>& nodes,
                            const std::vector<BranchItem*>& branches,
                            const PortConstraints& constraints) {
    const std::optional<BuildAttempt> attempt = tryBuildNormalTree(nodes, branches, constraints);
    return attempt && attempt->status == NormalTreeResult::Status::Ok;
}

void searchTwoPortAssignments(const std::vector<NodeItem*>& nodes,
                              const std::vector<BranchItem*>& branches,
                              const std::vector<TwoPortItem*>& twoPorts, int index,
                              PortConstraints constraints, bool& found, TreeScore& bestScore,
                              std::vector<BranchItem*>& bestTree) {
    if (index >= static_cast<int>(twoPorts.size())) {
        const std::optional<BuildAttempt> attempt = tryBuildNormalTree(nodes, branches, constraints);
        if (!attempt || attempt->status != NormalTreeResult::Status::Ok) {
            return;
        }
        const TreeScore score = scoreTree(attempt->treeBranches);
        if (!found || score.betterThan(bestScore)) {
            found = true;
            bestScore = score;
            bestTree = attempt->treeBranches;
        }
        return;
    }

    TwoPortItem* item = twoPorts[static_cast<size_t>(index)];
    if (!item) {
        searchTwoPortAssignments(nodes, branches, twoPorts, index + 1, std::move(constraints),
                                 found, bestScore, bestTree);
        return;
    }

    if (item->kind() == TwoPortKind::Transformer) {
        for (const bool rightInTree : {false, true}) {
            PortConstraints next = constraints;
            applyTransformerChoice(item, rightInTree, next);
            if (!constraintsAreFeasible(nodes, branches, next)) {
                continue;
            }
            searchTwoPortAssignments(nodes, branches, twoPorts, index + 1, std::move(next), found,
                                   bestScore, bestTree);
        }
        return;
    }

    for (const bool bothInTree : {false, true}) {
        PortConstraints next = constraints;
        applyGyratorChoice(item, bothInTree, next);
        if (!constraintsAreFeasible(nodes, branches, next)) {
            continue;
        }
        searchTwoPortAssignments(nodes, branches, twoPorts, index + 1, std::move(next), found,
                                 bestScore, bestTree);
    }
}

void extractStateVariables(NormalTreeResult& result, const std::vector<BranchItem*>& branches) {
    result.stateVariables.clear();
    const std::unordered_set<BranchItem*> inTree(result.treeBranches.begin(),
                                                  result.treeBranches.end());

    for (BranchItem* branch : result.treeBranches) {
        if (!branch || branch->isActive() ||
            effectivePassiveBranchType(*branch) != BranchType::A) {
            continue;
        }
        NormalTreeResult::StateVariable state;
        state.kind = NormalTreeResult::StateVariable::Kind::Across;
        state.symbol = branchAcrossSymbol(*branch);
        if (!state.symbol.isEmpty()) {
            result.stateVariables.push_back(std::move(state));
        }
    }

    for (BranchItem* branch : branches) {
        if (!branch || branch->isActive() ||
            effectivePassiveBranchType(*branch) != BranchType::T ||
            isTwoPortInternalBranch(*branch)) {
            continue;
        }
        if (inTree.count(branch) != 0) {
            continue;
        }
        NormalTreeResult::StateVariable state;
        state.kind = NormalTreeResult::StateVariable::Kind::Through;
        state.symbol = branchFlowSymbol(*branch);
        if (!state.symbol.isEmpty()) {
            result.stateVariables.push_back(std::move(state));
        }
    }
}

}  // namespace

void populateNormalTreeStateVariables(NormalTreeResult& result,
                                      const std::vector<BranchItem*>& branches) {
    extractStateVariables(result, branches);
}

NormalTreeResult computeNormalTree(const std::vector<NodeItem*>& nodes,
                                   const std::vector<BranchItem*>& branches,
                                   const std::vector<TwoPortItem*>& twoPorts) {
    NormalTreeResult result;
    const int n = static_cast<int>(nodes.size());
    if (n == 0) {
        result.status = NormalTreeResult::Status::Incomplete;
        result.message = QStringLiteral("The graph has no nodes.");
        return result;
    }
    if (n == 1) {
        result.status = NormalTreeResult::Status::Ok;
        extractStateVariables(result, branches);
        return result;
    }

    std::vector<TwoPortItem*> orderedTwoPorts;
    orderedTwoPorts.reserve(twoPorts.size());
    for (TwoPortItem* item : twoPorts) {
        if (item) {
            orderedTwoPorts.push_back(item);
        }
    }

    bool found = false;
    TreeScore bestScore;
    std::vector<BranchItem*> bestTree;

    if (orderedTwoPorts.empty()) {
        const std::optional<BuildAttempt> attempt =
            tryBuildNormalTree(nodes, branches, PortConstraints{});
        if (attempt && attempt->status == NormalTreeResult::Status::Ok) {
            found = true;
            bestTree = attempt->treeBranches;
        } else if (attempt) {
            result.status = attempt->status;
            result.message = attempt->status == NormalTreeResult::Status::ForcedCycle
                                 ? QStringLiteral("A-type active elements form a loop; "
                                                  "compatibility is violated.")
                                 : QStringLiteral("The graph is not connected enough to form a "
                                                  "normal tree with the current elements.");
            return result;
        }
    } else {
        searchTwoPortAssignments(nodes, branches, orderedTwoPorts, 0, PortConstraints{}, found,
                                 bestScore, bestTree);
    }

    if (!found) {
        result.status = orderedTwoPorts.empty() ? NormalTreeResult::Status::Incomplete
                                              : NormalTreeResult::Status::TwoPortConstraint;
        result.message = orderedTwoPorts.empty()
                             ? QStringLiteral("Could not form a spanning normal tree with the "
                                              "current elements and constraints.")
                             : QStringLiteral("Could not form a normal tree satisfying transformer "
                                              "(exactly one branch) and gyrator (zero or both "
                                              "branches) rules.");
        return result;
    }

    result.status = NormalTreeResult::Status::Ok;
    result.treeBranches = std::move(bestTree);
    extractStateVariables(result, branches);
    return result;
}

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
    return true;
}();

}  // namespace

}  // namespace lg
