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

void populateNormalTreeStateVariables(NormalTreeResult& result,
                                      const std::vector<BranchItem*>& branches);

NormalTreeResult computeNormalTree(const std::vector<NodeItem*>& nodes,
                                   const std::vector<BranchItem*>& branches,
                                   const std::vector<TwoPortItem*>& twoPorts = {});

}  // namespace lg
