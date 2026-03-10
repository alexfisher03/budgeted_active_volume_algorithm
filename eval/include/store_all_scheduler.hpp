#pragma once

#include "predicate_dag.hpp"
#include "scheduler_types.hpp"

#include <string>

struct ScheduleResult {
  std::vector<ScheduleAction> actions;
  ScheduleMetrics metrics;
  int root_node_id;
};

class StoreAllScheduler {
public:
  ScheduleResult run(const PredicateDag &dag) const;
};

void print_schedule_trace(const ScheduleResult &result,
                          const PredicateDag &dag);

void print_schedule_metrics(const ScheduleMetrics &metrics);

int run_store_all_demo(const std::string &json_path);
