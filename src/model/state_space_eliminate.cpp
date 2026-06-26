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

/*
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

    dots.clear();
    dots[QStringLiteral("Omega1")] =
        SymEngine::div(SymEngine::symbol("T_I1"), SymEngine::symbol("I1"));
    const std::vector<RCP<const Basic>> tI1Relations = {
        SymEngine::sub(SymEngine::symbol("T_I1"), SymEngine::symbol("Fs1")),
    };
    eliminateSymbolsInto(dots, tI1Relations, {QStringLiteral("T_I1")},
                         [](const QString&) { return true; });
    assert(SymEngine::eq(*expand(dots.at(QStringLiteral("Omega1"))),
                         *SymEngine::div(SymEngine::symbol("Fs1"), SymEngine::symbol("I1"))));

    dots.clear();
    dots[QStringLiteral("v1_dot")] =
        SymEngine::div(SymEngine::symbol("f_M"), SymEngine::symbol("M"));
    const RCP<const Basic> tfProd =
        SymEngine::mul(SymEngine::symbol("TF1"),
                       SymEngine::mul(SymEngine::symbol("TF2"), SymEngine::symbol("TF3")));
    const std::vector<RCP<const Basic>> massSpringRelations = {
        SymEngine::sub(SymEngine::symbol("f_M"),
                       SymEngine::sub(SymEngine::symbol("Fs1"),
                                      SymEngine::div(SymEngine::symbol("f_K"), tfProd))),
    };
    eliminateSymbolsInto(dots, massSpringRelations, {QStringLiteral("f_M")},
                         [](const QString&) { return true; });
    const RCP<const Basic> expectedV1Dot = SymEngine::add(
        SymEngine::div(SymEngine::symbol("Fs1"), SymEngine::symbol("M")),
        SymEngine::div(SymEngine::neg(SymEngine::symbol("f_K")),
                       SymEngine::mul(SymEngine::symbol("M"), tfProd)));
    assert(SymEngine::eq(*expand(dots.at(QStringLiteral("v1_dot"))), *expand(expectedV1Dot)));

    dots.clear();
    dots[QStringLiteral("OmegaJ")] =
        SymEngine::div(SymEngine::symbol("T_J"), SymEngine::symbol("J"));
    const std::vector<RCP<const Basic>> motorJunctionRelations = {
        SymEngine::sub(SymEngine::symbol("T_J"),
                       SymEngine::add(SymEngine::neg(SymEngine::symbol("T_port")),
                                      SymEngine::neg(SymEngine::symbol("T_B")))),
        SymEngine::sub(SymEngine::symbol("T_B"),
                       SymEngine::mul(SymEngine::symbol("B"), SymEngine::symbol("OmegaJ"))),
        SymEngine::sub(SymEngine::symbol("T_port"),
                       SymEngine::div(SymEngine::symbol("i_L"), SymEngine::symbol("Ka"))),
    };
    eliminateSymbolsInto(dots, motorJunctionRelations, {QStringLiteral("T_J"), QStringLiteral("T_B"),
                                                          QStringLiteral("T_port")},
                         [](const QString&) { return true; });
    resolveStateDotCoupling({QStringLiteral("OmegaJ")}, dots);
    const RCP<const Basic> expectedOmegaJDot = SymEngine::div(
        SymEngine::add(SymEngine::div(SymEngine::neg(SymEngine::symbol("i_L")),
                                      SymEngine::symbol("Ka")),
                       SymEngine::mul(SymEngine::neg(SymEngine::symbol("B")),
                                      SymEngine::symbol("OmegaJ"))),
        SymEngine::symbol("J"));
    assert(SymEngine::eq(*expand(dots.at(QStringLiteral("OmegaJ"))), *expand(expectedOmegaJDot)));
    assert(SymEngine::eq(*linearCoeffRCP(dots.at(QStringLiteral("OmegaJ")),
                                         dotSym(QStringLiteral("OmegaJ"))),
                         *integer(0)));

    return true;
}();

}  // namespace
*/

}  // namespace lg::ss
