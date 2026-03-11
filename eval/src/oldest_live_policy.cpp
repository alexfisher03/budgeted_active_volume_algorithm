#include "oldest_live_policy.hpp"

#include <climits>

std::optional<int> OldestLivePolicy::choose_eviction_candidate(
    const ScheduleState &state, const PredicateDag &dag,
    const std::unordered_set<int> &protected_set) const {

  std::optional<int> best;
  std::size_t best_step = SIZE_MAX;

  for (int nid : state.live_nodes) {
    // skip protected nodes
    if (protected_set.count(nid))
      continue;
    // skip nodes that cannot be legally uncomputed
    if (!sched::can_uncompute(nid, state, dag))
      continue;

    // find the node with the smallest live_since_step (oldest)
    auto it = state.live_since_step.find(nid);
    std::size_t step = (it != state.live_since_step.end()) ? it->second : 0;
    if (!best.has_value() || step < best_step) {
      best_step = step;
      best = nid;
    }
  }

  return best;
}
