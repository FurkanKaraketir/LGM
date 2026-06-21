#pragma once

#include "normal_tree.h"

#include <QString>
#include <QStringList>
#include <vector>

class BranchItem;
class NodeItem;

namespace lg {

struct StateSpaceResult {
    enum class Status { Ok, NeedNormalTree, Unsupported, SymbolicError, GraphError };

    Status status = Status::NeedNormalTree;
    QString message;
    QStringList stateVariables;
    QStringList inputs;
    QStringList inputLabels;
    QStringList elementalEquations;
    QStringList continuityEquations;
    QStringList compatibilityEquations;
    QStringList stateEquations;
    QString matrixForm;
};

StateSpaceResult computeStateSpace(const NormalTreeResult& tree,
                                   const std::vector<NodeItem*>& nodes,
                                   const std::vector<BranchItem*>& branches,
                                   const std::vector<class TwoPortItem*>& twoPorts = {});

}  // namespace lg
