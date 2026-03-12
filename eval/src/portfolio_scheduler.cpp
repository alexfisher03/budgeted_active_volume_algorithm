#include "portfolio_scheduler.hpp"
#include "depth_aware_policy.hpp"
#include "parser.hpp"
#include "recompute_aware_policy.hpp"
#include "smallest_fanout_policy.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

// portfolio scheduler::run
//
// instantiates the three portfolio member policies, runs each 
// independently through the generic BudgetedScheduler, then selects the 
// best feasible result by the deterministic tie-breaking rule

PortfolioResult PortfolioScheduler::run(const PredicateDag &dag,
                                        std::size_t budget,
                                        const DagAnalysis &analysis) const {
  // instantiate the three portfolio member policies
  // the order here defines the fixed priority for tie-break:
  //   index 0: smallest-fanout  (highest priority in tie-break)
  //   index 1: depth-aware
  //   index 2: recompute-aware  (lowest priority in tie-break)
  SmallestFanoutPolicy p_fanout(analysis);
  DepthAwarePolicy p_depth(analysis);
  RecomputeAwarePolicy p_recomp(analysis);

  const EvictionPolicy *policies[] = {&p_fanout, &p_depth, &p_recomp};
  constexpr std::size_t N = 3;

  BudgetedScheduler scheduler;
  PortfolioResult result;
  result.budget = budget;
  result.member_results.reserve(N);

  for (std::size_t i = 0; i < N; ++i) {
    result.member_results.push_back(scheduler.run(dag, budget, *policies[i]));
  }

  // selection 
  // find the best feasible result using the deterministic tie-break:
  //   1. lower total_cost
  //   2. lower peak_active_volume
  //   3. lower total_recomputations
  //   4. lower policy index (fixed priority order)

  result.winner_index = -1;
  result.feasible = false;

  for (std::size_t i = 0; i < N; ++i) {
    const auto &r = result.member_results[i];
    if (!r.feasible)
      continue;

    if (result.winner_index < 0) {
      // first feasible candidate
      result.winner_index = static_cast<int>(i);
      result.feasible = true;
      continue;
    }

    const auto &cur_best =
        result.member_results[static_cast<std::size_t>(result.winner_index)];
    const auto &m_new = r.metrics;
    const auto &m_old = cur_best.metrics;

    // Tie-break cascade:
    if (m_new.total_cost < m_old.total_cost) {
      result.winner_index = static_cast<int>(i);
    } else if (m_new.total_cost == m_old.total_cost) {
      if (m_new.peak_active_volume < m_old.peak_active_volume) {
        result.winner_index = static_cast<int>(i);
      } else if (m_new.peak_active_volume == m_old.peak_active_volume) {
        if (m_new.total_recomputations < m_old.total_recomputations) {
          result.winner_index = static_cast<int>(i);
        }
        // if all three criteria are equal, the current winner (lower index) keeps its position 
        // this is the fixed priority tie-break
      }
    }
  }

  return result;
}

// printing helpers

void print_portfolio_summary(const PortfolioResult &result,
                             const PredicateDag & /*dag*/) {
  std::cout << "\n=== PORTFOLIO SCHEDULER, B=" << result.budget << " ===\n";

  std::cout << "\n-- Member results --\n";
  for (std::size_t i = 0; i < result.member_results.size(); ++i) {
    const auto &r = result.member_results[i];
    const auto &m = r.metrics;
    bool is_winner =
        result.feasible && static_cast<int>(i) == result.winner_index;

    std::cout << "\n  [" << (i + 1) << "] " << r.policy_name;
    if (is_winner)
      std::cout << "  << WINNER";
    std::cout << "\n";
    std::cout << "      feasible             = "
              << (r.feasible ? "yes" : "no") << "\n";
    std::cout << "      peak_active_volume   = " << m.peak_active_volume
              << "\n";
    std::cout << "      total_compute_ops    = " << m.total_compute_ops
              << "\n";
    std::cout << "      total_uncompute_ops  = " << m.total_uncompute_ops
              << "\n";
    std::cout << "      total_recomputations = " << m.total_recomputations
              << "\n";
    std::cout << "      total_cost           = " << m.total_cost << "\n";
  }

  if (result.feasible) {
    const auto &w = result.winner();
    std::cout << "\n-- Selected winner --\n";
    std::cout << "  policy               = " << w.policy_name << "\n";
    std::cout << "  peak_active_volume   = " << w.metrics.peak_active_volume
              << "\n";
    std::cout << "  total_compute_ops    = " << w.metrics.total_compute_ops
              << "\n";
    std::cout << "  total_uncompute_ops  = " << w.metrics.total_uncompute_ops
              << "\n";
    std::cout << "  total_recomputations = " << w.metrics.total_recomputations
              << "\n";
    std::cout << "  total_cost           = " << w.metrics.total_cost << "\n";
  } else {
    std::cout << "\n-- Portfolio result: INFEASIBLE --\n";
    std::cout << "  All " << result.member_results.size()
              << " policies are infeasible at B=" << result.budget << ".\n";
  }
}

void print_portfolio_winner_trace(const PortfolioResult &result,
                                  const PredicateDag &dag) {
  if (!result.feasible) {
    std::cout << "\n(No trace available — portfolio is infeasible)\n";
    return;
  }
  print_budgeted_schedule_trace(result.winner(), dag);
}

// CLI entry points

int run_portfolio_demo(const std::string &json_path, std::size_t budget,
                       bool trace) {
  std::cout << "Loading predicate from: " << json_path << "\n";
  PredicateDag dag = load_predicate(json_path);
  std::cout << "Loaded " << dag.export_id << " (" << dag.inputs.size()
            << " inputs, " << dag.nodes.size()
            << " nodes, root_lit=" << dag.root_lit << ")\n";

  DagAnalysis analysis = build_dag_analysis(dag);

  PortfolioScheduler portfolio;
  PortfolioResult result = portfolio.run(dag, budget, analysis);

  print_portfolio_summary(result, dag);

  if (trace)
    print_portfolio_winner_trace(result, dag);

  std::cout << "\n";
  return result.feasible ? 0 : 1;
}

int run_portfolio_budget_list(const std::string &json_path, std::size_t lo,
                              std::size_t hi, const std::string &out_path) {
  PredicateDag dag = load_predicate(json_path);
  DagAnalysis analysis = build_dag_analysis(dag);

  std::ostream *out = &std::cout;
  std::ofstream file;
  if (!out_path.empty()) {
    file.open(out_path);
    if (!file.is_open()) {
      std::cerr << "Error: cannot open output file: " << out_path << "\n";
      return 1;
    }
    out = &file;
  }

  *out << "# scheduler: portfolio\n";
  *out << "# members: smallest-fanout, depth-aware, recompute-aware\n";
  *out << "# dag: " << dag.export_id << " (" << dag.nodes.size()
       << " internal nodes)\n\n";

  PortfolioScheduler portfolio;

  for (std::size_t b = lo; b <= hi; ++b) {
    PortfolioResult r = portfolio.run(dag, b, analysis);

    *out << "B = " << b << "\n";
    if (r.feasible) {
      const auto &w = r.winner();
      const auto &m = w.metrics;
      *out << "  feasible             = yes\n";
      *out << "  selected_policy      = " << w.policy_name << "\n";
      *out << "  peak_active_volume   = " << m.peak_active_volume << "\n";
      *out << "  total_compute_ops    = " << m.total_compute_ops << "\n";
      *out << "  total_uncompute_ops  = " << m.total_uncompute_ops << "\n";
      *out << "  total_recomputations = " << m.total_recomputations << "\n";
      *out << "  total_cost           = " << m.total_cost << "\n";
    } else {
      *out << "  feasible             = no\n";
    }
    if (b < hi)
      *out << "\n";
  }

  if (!out_path.empty())
    std::cerr << "Wrote " << (hi - lo + 1) << " entries to " << out_path
              << "\n";

  return 0;
}
