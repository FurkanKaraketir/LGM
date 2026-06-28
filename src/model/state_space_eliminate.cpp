#include "state_space_detail.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/subs.h>

#include <QDebug>

#include <cassert>

namespace lg::ss {

using SymEngine::eq;
using SymEngine::expand;
using SymEngine::integer;
using SymEngine::sub;
using SymEngine::subs;

bool substitutionIsCircular(const RCP<const Basic>& target, const RCP<const Basic>& primarySym,
                            const RCP<const Basic>& solved) {
    return eq(*expand(subs(target, {{primarySym, solved}})), *target);
}

void eliminateSymbolsInto(
    std::unordered_map<QString, RCP<const Basic>>& targets,
    const std::vector<RCP<const Basic>>& relations,
    const std::vector<QString>& candidates,
    const std::function<bool(const QString&)>& canEliminate,
    const std::function<bool(const RCP<const Basic>&)>& acceptSolution,
    const SubstitutionFilter& acceptSubstitution,
    const QString& phase) {
    if (candidates.empty() || relations.empty()) {
        return;
    }
    const QString stage = phase.isEmpty() ? QStringLiteral("eliminate") : phase;
    ssLog(stage, QStringLiteral("start relations=%1 candidates=%2 targets=%3")
                      .arg(relations.size())
                      .arg(candidates.size())
                      .arg(targets.size()));
    const size_t maxPasses = relations.size() * candidates.size() + candidates.size();
    for (size_t pass = 0; pass < maxPasses; ++pass) {
        bool progress = false;
        for (size_t relIdx = 0; relIdx < relations.size(); ++relIdx) {
            const RCP<const Basic>& relation = relations[relIdx];
            bool solvedRelation = false;
            for (const QString& candidate : candidates) {
                if (!canEliminate(candidate)) {
                    continue;
                }
                const RCP<const Basic> primarySym = symOf(candidate);
                const std::optional<RCP<const Basic>> solved =
                    solveLinearFor(relation, primarySym);
                if (!solved) {
                    continue;
                }
                if (acceptSolution && !acceptSolution(*solved)) {
                    continue;
                }
                bool changed = false;
                for (auto& [name, expr] : targets) {
                    if (acceptSubstitution &&
                        !acceptSubstitution(name, expr, primarySym, *solved)) {
                        continue;
                    }
                    if (substitutionIsCircular(expr, primarySym, *solved)) {
                        continue;
                    }
                    const RCP<const Basic> next = expand(subs(expr, {{primarySym, *solved}}));
                    if (!eq(*next, *expr)) {
                        ssLog(stage,
                              QStringLiteral("pass %1 rel[%2] %3 -> %4 in %5: %6")
                                  .arg(pass)
                                  .arg(relIdx)
                                  .arg(candidate, exprText(*solved), name, exprText(next)));
                        expr = next;
                        changed = true;
                    }
                }
                if (!changed) {
                    continue;
                }
                progress = true;
                solvedRelation = true;
                break;
            }
            (void)solvedRelation;
        }
        if (!progress) {
            ssLog(stage, QStringLiteral("pass %1 done (no progress)").arg(pass));
            break;
        }
        ssLog(stage, QStringLiteral("pass %1 done").arg(pass));
    }
    ssLog(stage, QStringLiteral("end"));
}

QString branchTautologyAcrossSymbol(const BranchItem& branch) {
    if (!usesSyntheticAcrossSymbol(branch)) {
        return {};
    }
    return branchAcrossSymbol(branch);
}

void eliminateBranchSymbolsInto(
    std::unordered_map<QString, RCP<const Basic>>& targets,
    const std::vector<RCP<const Basic>>& relations,
    const std::vector<BranchItem*>& branches,
    const std::unordered_set<BranchItem*>& treeSet,
    const std::function<bool(const QString&)>& canEliminate,
    const SubstitutionFilter& acceptSubstitution,
    const QString& phase) {
    (void)treeSet;
    std::vector<QString> candidates;
    candidates.reserve(branches.size() * 3);
    for (BranchItem* branch : branches) {
        if (!branch || branch->isActive()) {
            continue;
        }
        for (const QString& candidate :
             {branchThroughSymbol(*branch), branchAcrossSymbol(*branch)}) {
            if (!candidate.isEmpty() && canEliminate(candidate)) {
                candidates.push_back(candidate);
            }
        }
        const QString acrossText = branchAcrossVariableText(*branch);
        if (isValidVariableSymbol(acrossText) && canEliminate(acrossText)) {
            candidates.push_back(acrossText);
        }
    }
    eliminateSymbolsInto(targets, relations, candidates, canEliminate, {}, acceptSubstitution,
                         phase);
}

std::vector<RCP<const Basic>> constraintRelations(
    const std::unordered_map<QString, RCP<const Basic>>& replacements) {
    std::vector<RCP<const Basic>> relations;
    const std::unordered_map<QString, RCP<const Basic>> resolved =
        resolveReplacements(replacements);
    relations.reserve(resolved.size());
    for (const auto& [name, expr] : resolved) {
        if (!isValidVariableSymbol(name)) {
            continue;
        }
        relations.push_back(expand(sub(symOf(name), expr)));
    }
    return relations;
}



}  // namespace lg::ss
