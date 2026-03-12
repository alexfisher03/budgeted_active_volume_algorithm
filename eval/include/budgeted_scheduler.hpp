#pragma once

#include "eviction_policy.hpp"
#include "predicate_dag.hpp"
#include "scheduler_types.hpp"

#include <string>
#include <unordered_set>

struct BudgetedScheduleResult {
  bool feasible = false;
  std::size_t budget = 0;
  std::string policy_name;
  std::vector<ScheduleAction> actions;
  ScheduleMetrics metrics;
  int root_node_id = -1;
};

class BudgetedScheduler {
public:
  BudgetedScheduleResult run(const PredicateDag &dag, std::size_t budget,
                             const EvictionPolicy &policy) const;

private:
  // ensure node_id is live, recursively materializing fanins and evicting
  // under budget as needed.  returns false if infeasible.
  // inherited_protected: nodes the caller needs to remain live.
  // each recursion level makes a local copy and adds itself + ensured
  // fanins so deep-recursion additions do not leak back to the caller
  bool ensure_live(int node_id, ScheduleState &state, const PredicateDag &dag,
                   std::size_t budget, const EvictionPolicy &policy,
                   const std::unordered_set<int> &inherited_protected) const;

  // empty the live set after use-root, legally uncomputing all remaining
  // live nodes and re-materializing fanins as needed
  bool cleanup(ScheduleState &state, const PredicateDag &dag,
               std::size_t budget, const EvictionPolicy &policy) const;
};

void print_budgeted_schedule_trace(const BudgetedScheduleResult &result,
                                   const PredicateDag &dag);

void print_budgeted_schedule_metrics(const BudgetedScheduleResult &result);

int run_oldest_live_demo(const std::string &json_path, std::size_t budget);

// run oldest-live for every budget in [lo, hi] and write a metrics table
int run_budget_list(const std::string &json_path, std::size_t lo,
                    std::size_t hi, const std::string &out_path);
