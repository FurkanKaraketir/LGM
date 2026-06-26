#pragma once

#include <QString>
#include <vector>

class BranchItem;
class NodeItem;
class TwoPortItem;

namespace lg {

struct NormalTreeResult {
    enum class Status { Ok, NotConnected, ForcedCycle, Incomplete, TwoPortConstraint };

    struct StateVariable {
        enum class Kind { Across, Through };

        Kind kind = Kind::Across;
        QString symbol;
    };

    Status status = Status::Incomplete;
    QString message;
    std::vector<BranchItem*> treeBranches;
    std::vector<StateVariable> stateVariables;
};

struct NormalTreeEnumerationResult {
    NormalTreeResult::Status status = NormalTreeResult::Status::Incomplete;
    QString message;
    std::vector<NormalTreeResult> trees;
};

void populateNormalTreeStateVariables(NormalTreeResult& result,
                                      const std::vector<BranchItem*>& branches,
                                      const std::vector<TwoPortItem*>& twoPorts = {});

NormalTreeResult computeNormalTree(const std::vector<NodeItem*>& nodes,
                                   const std::vector<BranchItem*>& branches,
                                   const std::vector<TwoPortItem*>& twoPorts = {});

NormalTreeEnumerationResult enumerateNormalTrees(
    const std::vector<NodeItem*>& nodes, const std::vector<BranchItem*>& branches,
    const std::vector<TwoPortItem*>& twoPorts = {});

NormalTreeResult validateManualNormalTree(const std::vector<NodeItem*>& nodes,
                                          const std::vector<BranchItem*>& branches,
                                          const std::vector<TwoPortItem*>& twoPorts,
                                          const std::vector<BranchItem*>& treeBranches);

}  // namespace lg
