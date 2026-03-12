#pragma once

#include <cstddef>
#include <set>
#include <unordered_map>
#include <vector>

enum class ScheduleOpType { Compute, Uncompute, UseRoot };

struct ScheduleAction {
  ScheduleOpType op;
  int node_id;
  std::size_t live_count_after;
  std::size_t step;
};

struct ScheduleMetrics {
  std::size_t peak_active_volume = 0;
  std::size_t total_compute_ops = 0;
  std::size_t total_uncompute_ops = 0;
  std::size_t total_recomputations = 0;
  std::size_t total_cost = 0;
  std::size_t fallback_evictions = 0;
};

struct ScheduleState {
  std::set<int> live_nodes;
  std::unordered_map<int, int> compute_count;
  std::unordered_map<int, int> uncompute_count;
  std::unordered_map<int, std::size_t> live_since_step;
  std::vector<ScheduleAction> actions;
  std::size_t peak_active_volume = 0;
  int root_node_id = -1;
  bool root_used = false;
  std::size_t fallback_evictions = 0;
};
