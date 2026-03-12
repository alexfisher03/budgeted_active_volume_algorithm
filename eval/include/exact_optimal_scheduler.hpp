#pragma once

#include "predicate_dag.hpp"
#include "scheduler_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// exact optimal scheduler
//
// finds a minimum-cost legal schedule under AV <= B by exhaustive
// shortest-path search over the configuration graph
//
// state representation:
//   - live-set as a bitmask (uint32_t, supports up to 32 internal nodes)
//   - root_used flag
//   - combined into a single uint64_t key: (root_used << 32) | mask
//
// start state: live-set = empty, root_used = false
// goal state:  live-set = empty, root_used = true
//
// transitions:
//   Compute(v):   cost 1, add v to live-set (requires fanins available, |L|+1 <= B)
//   Uncompute(v): cost 1, remove v from live-set (requires v live, fanins available)
//   UseRoot:      cost 0, set root_used (requires root live)
//
// search: Dijkstra's algorithm with predecessor tracking
// trace: reconstructed from predecessor links after reaching goal

struct ExactOptimalResult {
  bool feasible = false;
  std::size_t budget = 0;
  std::vector<ScheduleAction> actions;
  ScheduleMetrics metrics;
  int root_node_id = -1;
  std::size_t states_explored = 0;
};

class ExactOptimalScheduler {
public:
  ExactOptimalResult run(const PredicateDag &dag, std::size_t budget) const;
};

// print full action trace and metrics for an exact optimal result
void print_exact_optimal_trace(const ExactOptimalResult &result,
                               const PredicateDag &dag);

void print_exact_optimal_metrics(const ExactOptimalResult &result);

// CLI entry: single budget run
int run_exact_optimal_demo(const std::string &json_path, std::size_t budget);

// CLI entry: budget sweep
int run_exact_optimal_budget_list(const std::string &json_path, std::size_t lo,
                                  std::size_t hi, const std::string &out_path);
