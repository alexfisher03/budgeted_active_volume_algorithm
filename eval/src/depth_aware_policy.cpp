#include "depth_aware_policy.hpp"

#include <climits>

std::optional<int> DepthAwarePolicy::choose_eviction_candidate(
    const ScheduleState &state, const PredicateDag &dag,
    const std::unordered_set<int> &protected_set) const {

  std::optional<int> best;
  int best_depth = INT_MAX;
  int best_id = INT_MAX;

  for (int nid : state.live_nodes) {
    if (protected_set.count(nid))
      continue;
    if (!sched::can_uncompute(nid, state, dag))
      continue;

    // look up precomputed structural depth, default to 0 if missing
    auto it = analysis_.depth.find(nid);
    int d = (it != analysis_.depth.end()) ? it->second : 0;

    // smallest depth (closest to inputs, cheapest to rebuild)
    // tie-break: smallest node id
    if (!best.has_value() || d < best_depth ||
        (d == best_depth && nid < best_id)) {
      best_depth = d;
      best_id = nid;
      best = nid;
    }
  }

  return best;
}
