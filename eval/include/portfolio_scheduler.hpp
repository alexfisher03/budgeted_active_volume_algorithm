#pragma once

#include "budgeted_scheduler.hpp"
#include "dag_analysis.hpp"
#include "predicate_dag.hpp"

#include <string>
#include <vector>

// portfolio scheduler
//
// Runs a fixed set of heuristic eviction policies on the same
// DAG and budget, then selects the lowest-cost feasible schedule
// deterministically

// portfolio members:
//   1. smallest-fanout policy
//   2. depth-aware policy
//   3. recompute-aware policy

// selection rule:
//   1. run each portfolio member independently
//   2. keep only feasible completed schedules
//   3. compare feasible schedules by total_cost
//   4. select the minimum-cost feasible schedule
//   5. if all members are infeasible, portfolio is infeasible

// deterministic tie-breaking:
//   1. lower total_cost
//   2. lower peak_active_volume
//   3. lower total_recomputations
//   4. fixed policy priority order

struct PortfolioResult {
  // did any member produce a feasible schedule
  bool feasible = false;

  // index of the winning policy in the members array (-1 if infeasible)
  int winner_index = -1;

  // budget used
  std::size_t budget = 0;

  // per-member results
  std::vector<BudgetedScheduleResult> member_results;

  // convenience accessors for the winning result
  const BudgetedScheduleResult &winner() const {
    return member_results.at(static_cast<std::size_t>(winner_index));
  }
};

class PortfolioScheduler {
public:
  // run all portfolio members on the given DAG and budget
  // analysis must have been built from the same DAG
  PortfolioResult run(const PredicateDag &dag, std::size_t budget,
                      const DagAnalysis &analysis) const;
};

// print portfolio member summaries and winner for a single budget run
void print_portfolio_summary(const PortfolioResult &result,
                             const PredicateDag &dag);

// print full schedule trace for the winning policy
void print_portfolio_winner_trace(const PortfolioResult &result,
                                  const PredicateDag &dag);

// CLI entry: single budget run
int run_portfolio_demo(const std::string &json_path, std::size_t budget,
                       bool trace);

// CLI entry: budget sweep, writes metrics for portfolio-best at each B
// if out_path is empty, writes to stdout
int run_portfolio_budget_list(const std::string &json_path, std::size_t lo,
                              std::size_t hi, const std::string &out_path);
