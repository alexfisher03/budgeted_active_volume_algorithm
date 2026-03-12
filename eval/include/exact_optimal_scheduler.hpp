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
// state: uint64_t with bits 0..62 = live-set mask, bit 63 = root_used
// supports up to 63 internal nodes
//
// start: live-set = empty, root_used = false
// goal:  live-set = empty, root_used = true
//
// transitions:
//   compute(v):   cost 1, requires fanins available, |L|+1 <= B
//   uncompute(v): cost 1, requires v live, fanins available
//   use_root:     cost 0, requires root live
//
// search: dijkstra with predecessor tracking
// B >= N shortcut: returns store-all (provably optimal)
// state limit: bails out at 50M states explored

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

void print_exact_optimal_trace(const ExactOptimalResult &result,
                               const PredicateDag &dag);

void print_exact_optimal_metrics(const ExactOptimalResult &result);

int run_exact_optimal_demo(const std::string &json_path, std::size_t budget);

int run_exact_optimal_budget_list(const std::string &json_path, std::size_t lo,
                                  std::size_t hi, const std::string &out_path);
