#include "state_space.h"
#include "state_space_context.h"

#include <unordered_map>

namespace lg {

StateSpaceResult computeStateSpaceImpl(const NormalTreeResult& tree,
                                       const std::vector<NodeItem*>& nodes,
                                       const std::vector<BranchItem*>& branches,
                                       const std::vector<TwoPortItem*>& twoPorts,
                                       const QStringList& outputVariables);

StateSpaceResult computeStateSpace(const NormalTreeResult& tree,
                                   const std::vector<NodeItem*>& nodes,
                                   const std::vector<BranchItem*>& branches,
                                   const std::vector<TwoPortItem*>& twoPorts,
                                   const QStringList& outputVariables) {
    StateSpaceResult result;
    ss::ssLog(QStringLiteral("compute"),
              QStringLiteral("nodes=%1 branches=%2 two_ports=%3 tree_status=%4")
                  .arg(nodes.size())
                  .arg(branches.size())
                  .arg(twoPorts.size())
                  .arg(static_cast<int>(tree.status)));
    (void)nodes;
    try {
        return computeStateSpaceImpl(tree, nodes, branches, twoPorts, outputVariables);
    } catch (const std::exception& e) {
        ss::ssLog(QStringLiteral("exception"), QString::fromUtf8(e.what()));
        result.status = StateSpaceResult::Status::SymbolicError;
        result.message = QStringLiteral("Symbolic processing failed: %1")
                             .arg(QString::fromUtf8(e.what()));
        return result;
    } catch (...) {
        ss::ssLog(QStringLiteral("exception"), QStringLiteral("unknown"));
        result.status = StateSpaceResult::Status::SymbolicError;
        result.message = QStringLiteral("Symbolic processing failed.");
        return result;
    }
}

StateSpaceResult computeStateSpaceImpl(const NormalTreeResult& tree,
                                       const std::vector<NodeItem*>& nodes,
                                       const std::vector<BranchItem*>& branches,
                                       const std::vector<TwoPortItem*>& twoPorts,
                                       const QStringList& outputVariables) {
    StateSpaceContext ctx(tree, nodes, branches, twoPorts);
    ctx.requestedOutputs = outputVariables;
    if (!ctx.initStates()) {
        return ctx.result;
    }
    if (!ssBuildConstraints(ctx)) {
        return ctx.result;
    }
    if (!ssReflectAndBind(ctx)) {
        return ctx.result;
    }
    std::unordered_map<QString, ss::RCP<const ss::Basic>> stateDots;
    if (!ssDeriveStateDots(ctx, stateDots)) {
        return ctx.result;
    }
    ssAssembleMatrix(ctx, stateDots);
    if (!ssAssembleOutputs(ctx, stateDots)) {
        return ctx.result;
    }
    ctx.result.status = StateSpaceResult::Status::Ok;
    ctx.result.message = QStringLiteral("State order %1, %2 continuity, %3 compatibility, %4 inputs.")
                             .arg(ctx.computedStates.size())
                             .arg(ctx.result.continuityEquations.size())
                             .arg(ctx.result.compatibilityEquations.size())
                             .arg(ctx.result.inputs.size());
    return ctx.result;
}

}  // namespace lg
