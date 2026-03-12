#include "diagnose_portfolio.hpp"
#include "budgeted_scheduler.hpp"
#include "dag_analysis.hpp"
#include "depth_aware_policy.hpp"
#include "diagnostic_policy.hpp"
#include "oldest_live_policy.hpp"
#include "parser.hpp"
#include "recompute_aware_policy.hpp"
#include "smallest_fanout_policy.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

// print the precomputed structural analysis tables
static void print_dag_analysis(const PredicateDag &dag,
                               const DagAnalysis &analysis) {
  std::vector<int> topo;
  for (const auto &n : dag.nodes)
    topo.push_back(n.id);
  std::sort(topo.begin(), topo.end());

  std::cout << "\n== DAG structural analysis ==\n\n";
  std::cout << "  node  fanout  depth  lhs_lit  rhs_lit\n";
  for (int nid : topo) {
    auto f_it = analysis.fanout_count.find(nid);
    int fanout = (f_it != analysis.fanout_count.end()) ? f_it->second : -1;
    auto d_it = analysis.depth.find(nid);
    int depth = (d_it != analysis.depth.end()) ? d_it->second : -1;
    auto n_it = dag.node_by_id.find(nid);
    int lhs = (n_it != dag.node_by_id.end()) ? n_it->second->lhs_lit : -1;
    int rhs = (n_it != dag.node_by_id.end()) ? n_it->second->rhs_lit : -1;
    std::cout << "  " << std::setw(4) << nid << "  " << std::setw(6) << fanout
              << "  " << std::setw(5) << depth << "  " << std::setw(7) << lhs
              << "  " << std::setw(7) << rhs << "\n";
  }
  std::cout << "\n";

  // check for nontrivial variation
  bool all_same_fanout = true;
  bool all_same_depth = true;
  int first_fanout = -1, first_depth = -1;
  for (int nid : topo) {
    int f = analysis.fanout_count.at(nid);
    int d = analysis.depth.at(nid);
    if (first_fanout < 0) {
      first_fanout = f;
      first_depth = d;
    } else {
      if (f != first_fanout)
        all_same_fanout = false;
      if (d != first_depth)
        all_same_depth = false;
    }
  }

  std::cout << "  fanout values: "
            << (all_same_fanout ? "ALL IDENTICAL" : "VARIED") << "\n";
  std::cout << "  depth values:  "
            << (all_same_depth ? "ALL IDENTICAL" : "VARIED") << "\n";
  std::cout << "\n";
}

// compare two action traces and report whether they are identical
static bool traces_identical(const BudgetedScheduleResult &a,
                             const BudgetedScheduleResult &b) {
  if (a.actions.size() != b.actions.size())
    return false;
  for (std::size_t i = 0; i < a.actions.size(); ++i) {
    if (a.actions[i].op != b.actions[i].op ||
        a.actions[i].node_id != b.actions[i].node_id)
      return false;
  }
  return true;
}

// print a condensed action trace
static const char *op_str(ScheduleOpType op) {
  switch (op) {
  case ScheduleOpType::Compute:
    return "Compute  ";
  case ScheduleOpType::Uncompute:
    return "Uncompute";
  case ScheduleOpType::UseRoot:
    return "UseRoot  ";
  }
  return "?";
}

static void print_trace(const BudgetedScheduleResult &r) {
  for (const auto &a : r.actions) {
    std::cout << "    [" << a.step << "] " << op_str(a.op) << "  node "
              << a.node_id << "  live_after=" << a.live_count_after << "\n";
  }
}

int run_diagnose_portfolio(const std::string &json_path, std::size_t budget) {
  std::cout << "Loading predicate from: " << json_path << "\n";
  PredicateDag dag = load_predicate(json_path);
  std::cout << "Loaded " << dag.export_id << " (" << dag.inputs.size()
            << " inputs, " << dag.nodes.size()
            << " nodes, root_lit=" << dag.root_lit << ")\n";

  DagAnalysis analysis = build_dag_analysis(dag);

  // print structural analysis first
  print_dag_analysis(dag, analysis);

  std::cout << "======================================================\n";
  std::cout << "DIAGNOSTIC: all policies at B = " << budget << "\n";
  std::cout << "======================================================\n";

  // set up the four policies to diagnose:
  // oldest-live (baseline) + three portfolio members
  OldestLivePolicy pol_oldest;
  SmallestFanoutPolicy pol_fanout(analysis);
  DepthAwarePolicy pol_depth(analysis);
  RecomputeAwarePolicy pol_recomp(analysis);

  struct PolicyEntry {
    const char *label;
    const EvictionPolicy *policy;
  };

  PolicyEntry entries[] = {
      {"oldest-live", &pol_oldest},
      {"smallest-fanout", &pol_fanout},
      {"depth-aware", &pol_depth},
      {"recompute-aware", &pol_recomp},
  };
  constexpr std::size_t N = 4;

  BudgetedScheduler scheduler;
  BudgetedScheduleResult results[N];

  for (std::size_t i = 0; i < N; ++i) {
    std::cout << "\n------------------------------------------------------\n";
    std::cout << "POLICY: " << entries[i].label << "\n";
    std::cout << "------------------------------------------------------\n\n";

    // wrap in diagnostic logger
    DiagnosticPolicy diag(*entries[i].policy, analysis, std::cout);
    results[i] = scheduler.run(dag, budget, diag);

    // print metrics
    const auto &m = results[i].metrics;
    std::cout << "  result: " << (results[i].feasible ? "FEASIBLE" : "INFEASIBLE")
              << "\n";
    std::cout << "  eviction_decisions   = " << diag.eviction_count() << "\n";
    std::cout << "  peak_active_volume   = " << m.peak_active_volume << "\n";
    std::cout << "  total_compute_ops    = " << m.total_compute_ops << "\n";
    std::cout << "  total_uncompute_ops  = " << m.total_uncompute_ops << "\n";
    std::cout << "  total_recomputations = " << m.total_recomputations << "\n";
    std::cout << "  total_cost           = " << m.total_cost << "\n";

    std::cout << "\n  -- action trace --\n";
    print_trace(results[i]);
  }

  // comparative analysis
  std::cout << "\n======================================================\n";
  std::cout << "TRACE COMPARISON\n";
  std::cout << "======================================================\n\n";

  // compare all pairs
  const char *labels[] = {"oldest-live", "smallest-fanout", "depth-aware",
                          "recompute-aware"};
  bool any_different = false;
  for (std::size_t i = 0; i < N; ++i) {
    for (std::size_t j = i + 1; j < N; ++j) {
      bool same = traces_identical(results[i], results[j]);
      std::cout << "  " << labels[i] << " vs " << labels[j] << ": "
                << (same ? "IDENTICAL" : "DIFFERENT") << "\n";
      if (!same)
        any_different = true;
    }
  }

  // find first divergence point if any pair differs
  if (any_different) {
    std::cout << "\n  first divergence points:\n";
    for (std::size_t i = 0; i < N; ++i) {
      for (std::size_t j = i + 1; j < N; ++j) {
        if (traces_identical(results[i], results[j]))
          continue;
        std::size_t len =
            std::min(results[i].actions.size(), results[j].actions.size());
        for (std::size_t k = 0; k < len; ++k) {
          if (results[i].actions[k].op != results[j].actions[k].op ||
              results[i].actions[k].node_id != results[j].actions[k].node_id) {
            std::cout << "    " << labels[i] << " vs " << labels[j]
                      << ": step " << k << " — " << labels[i] << " does "
                      << op_str(results[i].actions[k].op) << " node "
                      << results[i].actions[k].node_id << ", " << labels[j]
                      << " does " << op_str(results[j].actions[k].op)
                      << " node " << results[j].actions[k].node_id << "\n";
            break;
          }
        }
        if (results[i].actions.size() != results[j].actions.size()) {
          std::cout << "    " << labels[i] << " has "
                    << results[i].actions.size() << " actions, " << labels[j]
                    << " has " << results[j].actions.size() << "\n";
        }
      }
    }
  }

  std::cout << "\n======================================================\n";
  std::cout << "CONCLUSION\n";
  std::cout << "======================================================\n\n";

  if (!any_different) {
    std::cout
        << "  All four policies produce IDENTICAL action traces at B="
        << budget << ".\n"
        << "  This means every eviction decision had the same outcome\n"
        << "  regardless of which scoring criterion was used.\n\n"
        << "  Possible explanations:\n"
        << "  (a) FORCED-BY-STRUCTURE: at each eviction point there was\n"
        << "      exactly one legal candidate (all others were protected\n"
        << "      or could not be legally uncomputed), so the choice was\n"
        << "      forced regardless of scoring.\n"
        << "  (b) TIED-BY-SCORING: multiple candidates existed but all\n"
        << "      policies happened to pick the same one (e.g., the\n"
        << "      candidate with lowest fanout also had lowest depth\n"
        << "      and lowest recompute score).\n"
        << "  Review the eviction decision logs above to determine which.\n";
  } else {
    std::cout << "  Policies produce DIFFERENT action traces at B=" << budget
              << ".\n"
              << "  The portfolio is exercising genuine policy diversity.\n";
  }

  std::cout << "\n";
  return 0;
}
