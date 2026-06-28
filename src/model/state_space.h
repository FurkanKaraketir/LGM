#pragma once

#include "normal_tree.h"

#include <QString>
#include <QStringList>
#include <vector>

class BranchItem;
class NodeItem;

namespace lg {

struct GraphOutputVariable {
    QString symbol;
    QString label;
};

struct StateSpaceMatrices {
    std::vector<QStringList> A;
    std::vector<QStringList> B;
    std::vector<QStringList> E;
    std::vector<QStringList> C;
    std::vector<QStringList> D;
    std::vector<QStringList> F;
    QStringList parameters;
};

struct StateSpaceResult {
    enum class Status { Ok, NeedNormalTree, Unsupported, SymbolicError, GraphError };

    Status status = Status::NeedNormalTree;
    QString message;
    QStringList stateVariables;
    QStringList inputs;
    QStringList inputLabels;
    QStringList outputs;
    QStringList outputLabels;
    QStringList elementalEquations;
    QStringList continuityEquations;
    QStringList compatibilityEquations;
    QStringList stateEquations;
    QStringList outputEquations;
    QString matrixForm;
    QString outputMatrixForm;
    StateSpaceMatrices matrices;
};

QString generateMatlabScript(const StateSpaceResult& result);

std::vector<GraphOutputVariable> collectOutputVariableChoices(
    const std::vector<NodeItem*>& nodes, const std::vector<BranchItem*>& branches);

StateSpaceResult computeStateSpace(const NormalTreeResult& tree,
                                   const std::vector<NodeItem*>& nodes,
                                   const std::vector<BranchItem*>& branches,
                                   const std::vector<class TwoPortItem*>& twoPorts = {},
                                   const QStringList& outputVariables = {});

}  // namespace lg
