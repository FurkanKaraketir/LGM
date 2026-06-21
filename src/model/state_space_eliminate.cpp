#include "state_space_detail.h"

#include "canvas.h"
#include "elemental_equation.h"

#include <symengine/add.h>
#include <symengine/integer.h>
#include <symengine/mul.h>
#include <symengine/subs.h>

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
    const SubstitutionFilter& acceptSubstitution) {
    if (candidates.empty() || relations.empty()) {
        return;
    }
    const size_t maxPasses = relations.size() * candidates.size() + candidates.size();
    for (size_t pass = 0; pass < maxPasses; ++pass) {
        bool progress = false;
        for (const RCP<const Basic>& relation : relations) {
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
            break;
        }
    }
}

QString branchTautologyAcrossSymbol(const BranchItem& branch) {
    const QString text = branchAcrossVariableText(branch).trimmed();
    if (text.isEmpty() || text == QStringLiteral("0") || isValidVariableSymbol(text)) {
        return {};
    }
    return branchFlowSymbol(branch) + QStringLiteral("_a");
}

void eliminateBranchSymbolsInto(
    std::unordered_map<QString, RCP<const Basic>>& targets,
    const std::vector<RCP<const Basic>>& relations,
    const std::vector<BranchItem*>& branches,
    const std::unordered_set<BranchItem*>& treeSet,
    const std::function<bool(const QString&)>& canEliminate) {
    std::vector<QString> candidates;
    candidates.reserve(branches.size() * 3);
    for (BranchItem* branch : branches) {
        if (!branch || branch->isActive()) {
            continue;
        }
        const bool inTree = treeSet.count(branch) != 0;
        const bool twoPortPort = isTwoPortInternalBranch(*branch);
        const BranchType type = effectivePassiveBranchType(*branch);
        // Two-port ports use BranchType::T by convention, not as through-storage elements.
        if (!twoPortPort && type == BranchType::A && inTree) {
            continue;
        }
        if (!twoPortPort && type == BranchType::T && !inTree) {
            continue;
        }
        for (const QString& candidate :
             {branchFlowSymbol(*branch), branchAcrossSymbol(*branch)}) {
            if (!candidate.isEmpty()) {
                candidates.push_back(candidate);
            }
        }
        const QString acrossText = branchAcrossVariableText(*branch);
        if (isValidVariableSymbol(acrossText)) {
            candidates.push_back(acrossText);
        }
    }
    eliminateSymbolsInto(targets, relations, candidates, canEliminate);
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

namespace {

const bool kEliminateSelfCheck = [] {
    std::unordered_map<QString, RCP<const Basic>> dots;
    dots[QStringLiteral("i_L7")] = SymEngine::add(
        SymEngine::div(SymEngine::neg(SymEngine::symbol("V3")), SymEngine::symbol("L7")),
        SymEngine::div(SymEngine::symbol("Vs1"), SymEngine::symbol("L7")));
    const std::vector<RCP<const Basic>> relations = {
        SymEngine::sub(SymEngine::symbol("V3"), SymEngine::symbol("V2"))};
    eliminateSymbolsInto(dots, relations, {QStringLiteral("V3")},
                         [](const QString&) { return true; });
    assert(SymEngine::eq(*expand(dots.at(QStringLiteral("i_L7"))),
                         *SymEngine::div(SymEngine::sub(SymEngine::symbol("V1"),
                                                        SymEngine::symbol("V2")),
                                        SymEngine::symbol("L7"))));

    dots.clear();
    dots[QStringLiteral("i_L7")] = SymEngine::add(
        SymEngine::div(SymEngine::neg(SymEngine::symbol("V3")), SymEngine::symbol("L7")),
        SymEngine::div(SymEngine::symbol("Vs1"), SymEngine::symbol("L7")));
    const std::vector<RCP<const Basic>> l7Relations = {
        SymEngine::sub(SymEngine::symbol("i_L7_a"),
                       SymEngine::sub(SymEngine::symbol("Vs1"), SymEngine::symbol("V3"))),
        SymEngine::sub(SymEngine::add(SymEngine::add(SymEngine::neg(SymEngine::symbol("Is2")),
                                                     SymEngine::symbol("i_L7")),
                                      SymEngine::div(SymEngine::symbol("V4"),
                                                     SymEngine::symbol("R2"))),
                       SymEngine::div(SymEngine::symbol("V3"), SymEngine::symbol("R2"))),
        SymEngine::sub(SymEngine::symbol("V4"),
                       SymEngine::mul(SymEngine::symbol("L3"),
                                      SymEngine::sub(dotSym(QStringLiteral("i_L7")),
                                                     dotSym(QStringLiteral("Is2"))))),
    };
    const auto rejectL7Across = [](const QString& targetName,
                                   const RCP<const Basic>& targetExpr,
                                   const RCP<const Basic>& primarySym,
                                   const RCP<const Basic>& solved) {
        if (targetName == QStringLiteral("i_L7") &&
            !SymEngine::eq(*linearCoeffRCP(solved, SymEngine::symbol("i_L7_a")), *integer(0))) {
            return false;
        }
        return !substitutionIsCircular(targetExpr, primarySym, solved);
    };
    eliminateSymbolsInto(dots, l7Relations, {QStringLiteral("V3"), QStringLiteral("V4")},
                         [](const QString&) { return true; }, {}, rejectL7Across);
    resolveStateDotCoupling({QStringLiteral("i_L7")}, dots);
    const RCP<const Basic> expected = SymEngine::div(
        SymEngine::add(SymEngine::symbol("Vs1"),
                       SymEngine::add(SymEngine::mul(SymEngine::symbol("L3"),
                                                     dotSym(QStringLiteral("Is2"))),
                                      SymEngine::mul(SymEngine::symbol("R2"),
                                                     SymEngine::sub(SymEngine::symbol("i_L7"),
                                                                    SymEngine::symbol("Is2"))))),
        SymEngine::add(SymEngine::symbol("L7"), SymEngine::symbol("L3")));
    assert(SymEngine::eq(*expand(dots.at(QStringLiteral("i_L7"))), *expand(expected)));

    dots.clear();
    dots[QStringLiteral("i_L7")] = SymEngine::add(
        SymEngine::div(SymEngine::neg(SymEngine::symbol("V3")), SymEngine::symbol("L7")),
        SymEngine::div(SymEngine::symbol("Vs1"), SymEngine::symbol("L7")));
    const std::vector<RCP<const Basic>> tautologyRelations = {
        SymEngine::sub(SymEngine::symbol("i_L7_a"),
                       SymEngine::sub(SymEngine::symbol("Vs1"), SymEngine::symbol("V3")))};
    const auto rejectTautology = [](const QString& targetName,
                                    const RCP<const Basic>& targetExpr,
                                    const RCP<const Basic>& primarySym,
                                    const RCP<const Basic>& solved) {
        if (targetName == QStringLiteral("i_L7") &&
            !SymEngine::eq(*linearCoeffRCP(solved, SymEngine::symbol("i_L7_a")), *integer(0))) {
            return false;
        }
        return !substitutionIsCircular(targetExpr, primarySym, solved);
    };
    eliminateSymbolsInto(dots, tautologyRelations, {QStringLiteral("V3")},
                         [](const QString&) { return true; }, {}, rejectTautology);
    assert(SymEngine::eq(*dots.at(QStringLiteral("i_L7")),
                         *SymEngine::add(SymEngine::div(SymEngine::neg(SymEngine::symbol("V3")),
                                                        SymEngine::symbol("L7")),
                                         SymEngine::div(SymEngine::symbol("Vs1"),
                                                        SymEngine::symbol("L7")))));

    dots.clear();
    dots[QStringLiteral("v2")] = SymEngine::div(SymEngine::neg(SymEngine::symbol("f2")),
                                                SymEngine::symbol("m"));
    const std::vector<RCP<const Basic>> tfRelations = {
        SymEngine::add(SymEngine::symbol("Fs1"),
                       SymEngine::div(SymEngine::symbol("f2"), SymEngine::symbol("TF"))),
    };
    eliminateSymbolsInto(dots, tfRelations, {QStringLiteral("f2")},
                         [](const QString&) { return true; });
    assert(SymEngine::eq(*expand(dots.at(QStringLiteral("v2"))),
                         *SymEngine::div(SymEngine::mul(SymEngine::symbol("TF"),
                                                        SymEngine::symbol("Fs1")),
                                        SymEngine::symbol("m"))));
    return true;
}();

}  // namespace

}  // namespace lg::ss
